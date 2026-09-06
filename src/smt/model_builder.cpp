#include "smt/model_builder.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/sort.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/model_reconstruction.hpp"
#include "theory/equality/incremental_congruence_closure.hpp"
#include "util/rational.hpp"

namespace primo::smt {

namespace {

struct VectorKey {
  std::vector<std::string> values;

  bool operator==(const VectorKey& other) const = default;
};

struct VectorKeyHash {
  std::size_t operator()(const VectorKey& key) const {
    std::size_t seed = 0;
    for (const std::string& value : key.values) {
      seed ^= std::hash<std::string>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    }
    return seed;
  }
};

struct TableEntry {
  std::vector<std::string> args;
  std::string result;
  bool has_exact_numeric_value = false;
  bool constrained = false;
};

bool is_internal_symbol(const core::Symbol& symbol) { return symbol.name.starts_with("$primo_"); }

std::string abstract_value_name(core::SortId sort, std::size_t index) {
  return "@uc_" + std::to_string(sort) + "_" + std::to_string(index);
}

std::string smtlib_rational(const util::Rational& value) {
  const std::string text = value.to_string();
  const std::size_t slash = text.find('/');
  if (slash == std::string::npos) {
    if (text.starts_with('-')) {
      return "(- " + text.substr(1) + ")";
    }
    return text;
  }
  const bool negative = text.starts_with('-');
  const std::string numerator = negative ? text.substr(1, slash - 1) : text.substr(0, slash);
  const std::string denominator = text.substr(slash + 1);
  const std::string fraction = "(/ " + numerator + " " + denominator + ")";
  return negative ? "(- " + fraction + ")" : fraction;
}

class QfUfModelBuilder {
 public:
  QfUfModelBuilder(core::TermManager& terms, const std::vector<TheoryModelAssignment>& assignments,
                   QfUfModelBuildOptions options)
      : terms_(terms), options_(options), closure_(terms) {
    true_term_ = terms_.mk_bool(true);
    false_term_ = terms_.mk_bool(false);
    closure_.add_term(true_term_);
    closure_.add_term(false_term_);
    add_all_relevant_terms();
    for (const TheoryModelAssignment& assignment : assignments) {
      if (assignment.filtered) {
        continue;
      }
      add_assignment(assignment);
    }
    closure_.saturate();
    complete_boolean_values();
    index_numeric_values();
  }

  std::string build() {
    seed_defaults();
    std::ostringstream out;
    if (options_.include_model_wrapper) {
      out << "(model\n";
    }
    for (const core::Symbol& symbol : terms_.symbols().symbols()) {
      if (skip_symbol(symbol)) {
        continue;
      }
      if (symbol.kind == core::SymbolKind::Constant) {
        out << "  (define-fun " << symbol.name << " () " << sort_name(symbol.result_sort) << " "
            << constant_value(symbol) << ")\n";
      }
    }
    for (const core::Symbol& symbol : terms_.symbols().symbols()) {
      if (skip_symbol(symbol) || symbol.kind == core::SymbolKind::Constant) {
        continue;
      }
      const std::vector<std::string> parameters = model_parameter_names(symbol.arg_sorts.size());
      out << "  (define-fun " << symbol.name << " (";
      for (std::size_t i = 0; i < symbol.arg_sorts.size(); ++i) {
        if (i != 0) {
          out << " ";
        }
        out << "(" << parameters[i] << " " << sort_name(symbol.arg_sorts[i]) << ")";
      }
      out << ") " << sort_name(symbol.result_sort) << " " << function_body(symbol, parameters)
          << ")\n";
    }
    if (options_.include_model_wrapper) {
      out << ")";
    }
    return out.str();
  }

 private:
  bool skip_symbol(const core::Symbol& symbol) const {
    return is_internal_symbol(symbol) ||
           (options_.skip_real_symbols && symbol.result_sort == terms_.symbols().real_sort()) ||
           (options_.skip_real_constants && symbol.kind == core::SymbolKind::Constant &&
            symbol.result_sort == terms_.symbols().real_sort());
  }

  void add_all_relevant_terms() {
    for (const core::Term& term : terms_.terms()) {
      if (term.kind == core::TermKind::Constant || term.kind == core::TermKind::Application) {
        closure_.add_term(term.id);
      }
    }
  }

  void complete_boolean_values() {
    // A Boolean constant or application absent from the final SAT problem is
    // printed as false. Make that completion choice inside the congruence
    // closure as well: otherwise two applications can print with identical
    // Boolean arguments while retaining different result classes.
    for (const core::Term& term : terms_.terms()) {
      if (term.sort != terms_.symbols().bool_sort() ||
          (term.kind != core::TermKind::Constant && term.kind != core::TermKind::Application)) {
        continue;
      }
      if (!closure_.are_equal(term.id, true_term_) && !closure_.are_equal(term.id, false_term_)) {
        closure_.assert_equal(term.id, false_term_);
      }
    }
    closure_.saturate();
  }

  void add_assignment(const TheoryModelAssignment& assignment) {
    const core::Term& term = terms_.get(assignment.term);
    if (term.kind == core::TermKind::Equality) {
      if (assignment.value) {
        constrained_terms_.insert(term.children.at(0));
        constrained_terms_.insert(term.children.at(1));
        closure_.assert_equal(term.children.at(0), term.children.at(1));
      }
      return;
    }
    if (term.sort == terms_.symbols().bool_sort() &&
        (term.kind == core::TermKind::Constant || term.kind == core::TermKind::Application)) {
      constrained_terms_.insert(assignment.term);
      closure_.assert_equal(assignment.term, assignment.value ? true_term_ : false_term_);
    }
  }

  void seed_defaults() {
    for (const core::Symbol& symbol : terms_.symbols().symbols()) {
      if (skip_symbol(symbol)) {
        continue;
      }
      if (symbol.kind == core::SymbolKind::Constant &&
          symbol.result_sort != terms_.symbols().bool_sort()) {
        (void)value_name(closure_.class_representative(model_constant_term(symbol.id)));
      }
    }
    for (const core::Term& term : terms_.terms()) {
      if (term.kind == core::TermKind::Application &&
          skip_symbol(terms_.symbols().get_symbol(term.symbol))) {
        continue;
      }
      if (term.kind == core::TermKind::Constant &&
          skip_symbol(terms_.symbols().get_symbol(term.symbol))) {
        continue;
      }
      if ((term.kind == core::TermKind::Constant || term.kind == core::TermKind::Application) &&
          term.sort != terms_.symbols().real_sort() && term.sort != terms_.symbols().bool_sort()) {
        (void)value_name(closure_.class_representative(term.id));
      }
    }
    for (const core::Sort& sort : terms_.symbols().sorts()) {
      if (sort.kind == core::SortKind::Uninterpreted) {
        (void)default_value(sort.id);
      }
    }
  }

  std::string sort_name(core::SortId sort) const { return terms_.symbols().get_sort(sort).name; }

  std::vector<std::string> model_parameter_names(std::size_t arity) const {
    std::vector<std::string> parameters;
    parameters.reserve(arity);
    for (std::size_t index = 0; index < arity; ++index) {
      std::string candidate = "x" + std::to_string(index);
      if (terms_.symbols().lookup_symbol(candidate).has_value()) {
        candidate = "@primo_model_arg_" + std::to_string(index);
        std::size_t suffix = 0;
        while (terms_.symbols().lookup_symbol(candidate).has_value() ||
               std::ranges::find(parameters, candidate) != parameters.end()) {
          candidate = "@primo_model_arg_" + std::to_string(index) + "_" + std::to_string(suffix++);
        }
      }
      parameters.push_back(std::move(candidate));
    }
    return parameters;
  }

  std::string value_name(core::TermId representative) {
    const core::SortId sort = terms_.get(representative).sort;
    auto& values = values_by_sort_[sort];
    const auto found = values.find(representative);
    if (found != values.end()) {
      return found->second;
    }
    const std::string name = abstract_value_term(sort, next_value_index(sort));
    values.emplace(representative, name);
    return name;
  }

  std::string default_value(core::SortId sort) {
    auto found = default_value_by_sort_.find(sort);
    if (found != default_value_by_sort_.end()) {
      return found->second;
    }
    const std::string name = abstract_value_term(sort, next_value_index(sort));
    default_value_by_sort_.emplace(sort, name);
    return name;
  }

  std::string abstract_value_term(core::SortId sort, std::size_t index) const {
    return "(as " + abstract_value_name(sort, index) + " " + sort_name(sort) + ")";
  }

  std::size_t next_value_index(core::SortId sort) {
    const std::size_t index = next_value_index_by_sort_[sort];
    next_value_index_by_sort_[sort] = index + 1;
    return index;
  }

  core::TermId model_constant_term(core::SymbolId symbol) {
    const core::TermId term = terms_.mk_constant(symbol);
    closure_.add_term(term);
    return term;
  }

  bool bool_value(core::TermId term) {
    const core::Term& node = terms_.get(term);
    if (node.kind == core::TermKind::BoolValue) {
      return node.bool_value;
    }
    if (closure_.are_equal(term, true_term_)) {
      return true;
    }
    return false;
  }

  void index_numeric_values() {
    const auto index = [this](const std::unordered_map<core::TermId, util::Rational>* source,
                              core::SortId sort,
                              std::unordered_map<core::TermId, util::Rational>& by_representative) {
      if (source == nullptr) {
        return;
      }
      for (const auto& [term, value] : *source) {
        if (terms_.get(term).sort != sort) {
          continue;
        }
        const core::TermId representative = closure_.class_representative(term);
        const auto [existing, inserted] = by_representative.emplace(representative, value);
        if (!inserted && existing->second != value) {
          throw std::logic_error("component model values disagree inside one EUF class");
        }
      }
    };
    index(options_.real_values, terms_.symbols().real_sort(), real_values_by_representative_);
    index(options_.int_values, terms_.symbols().int_sort(), int_values_by_representative_);
    for (const auto& [term, value] : int_values_by_representative_) {
      static_cast<void>(term);
      used_int_values_.insert(value);
    }
  }

  util::Rational fresh_int_value(core::TermId representative) {
    const auto found = generated_int_values_.find(representative);
    if (found != generated_int_values_.end()) {
      return found->second;
    }
    util::Rational candidate(0);
    while (used_int_values_.contains(candidate)) {
      candidate = candidate + util::Rational(1);
    }
    used_int_values_.insert(candidate);
    generated_int_values_.emplace(representative, candidate);
    if (options_.invented_int_values != nullptr) {
      ++*options_.invented_int_values;
    }
    return candidate;
  }

  std::string term_value(core::TermId term) {
    const core::Term& node = terms_.get(term);
    if (node.sort == terms_.symbols().bool_sort()) {
      return bool_value(term) ? "true" : "false";
    }
    if (node.sort == terms_.symbols().real_sort() && options_.real_values != nullptr) {
      const auto found = options_.real_values->find(term);
      if (found != options_.real_values->end()) {
        return smtlib_rational(found->second);
      }
      const auto representative_found =
          real_values_by_representative_.find(closure_.class_representative(term));
      return smtlib_rational(representative_found == real_values_by_representative_.end()
                                 ? util::Rational(0)
                                 : representative_found->second);
    }
    if (node.sort == terms_.symbols().int_sort() && options_.int_values != nullptr) {
      const auto found = options_.int_values->find(term);
      if (found != options_.int_values->end()) {
        return smtlib_rational(found->second);
      }
      const core::TermId representative = closure_.class_representative(term);
      const auto representative_found = int_values_by_representative_.find(representative);
      return smtlib_rational(representative_found == int_values_by_representative_.end()
                                 ? fresh_int_value(representative)
                                 : representative_found->second);
    }
    return value_name(closure_.class_representative(term));
  }

  std::string constant_value(const core::Symbol& symbol) {
    const core::TermId term = model_constant_term(symbol.id);
    return term_value(term);
  }

  std::vector<TableEntry> table_entries(const core::Symbol& symbol) {
    std::unordered_map<VectorKey, std::size_t, VectorKeyHash> seen;
    std::vector<TableEntry> entries;
    for (const core::Term& term : terms_.terms()) {
      if (term.kind != core::TermKind::Application || term.symbol != symbol.id) {
        continue;
      }
      if (symbol.result_sort == terms_.symbols().bool_sort() && !bool_value(term.id)) {
        continue;
      }
      VectorKey key;
      key.values.reserve(term.children.size());
      for (const core::TermId child : term.children) {
        key.values.push_back(term_value(child));
      }
      const std::string result = term_value(term.id);
      const bool has_exact_numeric_value =
          (symbol.result_sort == terms_.symbols().real_sort() && options_.real_values != nullptr &&
           options_.real_values->contains(term.id)) ||
          (symbol.result_sort == terms_.symbols().int_sort() && options_.int_values != nullptr &&
           int_values_by_representative_.contains(closure_.class_representative(term.id)));
      const bool constrained = constrained_terms_.contains(term.id);
      const auto found = seen.find(key);
      if (found == seen.end()) {
        seen.emplace(key, entries.size());
        entries.push_back(
            TableEntry{std::move(key.values), result, has_exact_numeric_value, constrained});
      } else {
        TableEntry& current = entries.at(found->second);
        if ((has_exact_numeric_value && !current.has_exact_numeric_value) ||
            (has_exact_numeric_value == current.has_exact_numeric_value && constrained &&
             !current.constrained)) {
          current.result = result;
          current.has_exact_numeric_value = has_exact_numeric_value;
          current.constrained = constrained;
        }
      }
    }
    std::sort(entries.begin(), entries.end(),
              [](const TableEntry& lhs, const TableEntry& rhs) { return lhs.args < rhs.args; });
    return entries;
  }

  std::string condition_for_entry(const TableEntry& entry,
                                  const std::vector<std::string>& parameters) const {
    if (entry.args.empty()) {
      return "true";
    }
    if (entry.args.size() == 1) {
      return "(= " + parameters.front() + " " + entry.args.front() + ")";
    }
    std::ostringstream out;
    out << "(and";
    for (std::size_t i = 0; i < entry.args.size(); ++i) {
      out << " (= " << parameters[i] << " " << entry.args[i] << ")";
    }
    out << ")";
    return out.str();
  }

  std::string default_result(const core::Symbol& symbol) {
    if (symbol.result_sort == terms_.symbols().bool_sort()) {
      return "false";
    }
    if (symbol.result_sort == terms_.symbols().real_sort()) {
      return "0";
    }
    if (symbol.result_sort == terms_.symbols().int_sort() && options_.int_values != nullptr) {
      return "0";
    }
    return default_value(symbol.result_sort);
  }

  std::string function_body(const core::Symbol& symbol,
                            const std::vector<std::string>& parameters) {
    std::string body = default_result(symbol);
    std::vector<TableEntry> entries = table_entries(symbol);
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
      if (it->result == body) {
        continue;
      }
      body = "(ite " + condition_for_entry(*it, parameters) + " " + it->result + " " + body + ")";
    }
    return body;
  }

  core::TermManager& terms_;
  QfUfModelBuildOptions options_;
  theory::equality::IncrementalCongruenceClosure closure_;
  core::TermId true_term_ = core::kInvalidTermId;
  core::TermId false_term_ = core::kInvalidTermId;
  std::unordered_map<core::SortId, std::unordered_map<core::TermId, std::string>> values_by_sort_;
  std::unordered_map<core::SortId, std::string> default_value_by_sort_;
  std::unordered_map<core::SortId, std::size_t> next_value_index_by_sort_;
  std::unordered_set<core::TermId> constrained_terms_;
  std::unordered_map<core::TermId, util::Rational> real_values_by_representative_;
  std::unordered_map<core::TermId, util::Rational> int_values_by_representative_;
  std::unordered_map<core::TermId, util::Rational> generated_int_values_;
  std::unordered_set<util::Rational, util::RationalHash> used_int_values_;
};

}  // namespace

std::string build_qf_uf_model(core::TermManager& terms,
                              const std::vector<TheoryModelAssignment>& theory_assignments,
                              QfUfModelBuildOptions options) {
  QfUfModelBuilder builder(terms, theory_assignments, options);
  return builder.build();
}

}  // namespace primo::smt
