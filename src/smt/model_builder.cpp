#include "smt/model_builder.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/sort.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "theory/equality/incremental_congruence_closure.hpp"

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
};

bool is_internal_symbol(const core::Symbol& symbol) { return symbol.name.starts_with("$primo_"); }

std::string abstract_value_name(core::SortId sort, std::size_t index) {
  return "@uc_" + std::to_string(sort) + "_" + std::to_string(index);
}

class QfUfModelBuilder {
 public:
  QfUfModelBuilder(core::TermManager& terms, const std::vector<TheoryModelAssignment>& assignments)
      : terms_(terms), closure_(terms) {
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
  }

  std::string build() {
    seed_defaults();
    std::ostringstream out;
    out << "(model\n";
    for (const core::Symbol& symbol : terms_.symbols().symbols()) {
      if (is_internal_symbol(symbol)) {
        continue;
      }
      if (symbol.kind == core::SymbolKind::Constant) {
        out << "  (define-fun " << symbol.name << " () " << sort_name(symbol.result_sort) << " "
            << constant_value(symbol) << ")\n";
      }
    }
    for (const core::Symbol& symbol : terms_.symbols().symbols()) {
      if (is_internal_symbol(symbol) || symbol.kind == core::SymbolKind::Constant) {
        continue;
      }
      out << "  (define-fun " << symbol.name << " (";
      for (std::size_t i = 0; i < symbol.arg_sorts.size(); ++i) {
        if (i != 0) {
          out << " ";
        }
        out << "(x" << i << " " << sort_name(symbol.arg_sorts[i]) << ")";
      }
      out << ") " << sort_name(symbol.result_sort) << " " << function_body(symbol) << ")\n";
    }
    out << ")";
    return out.str();
  }

 private:
  void add_all_relevant_terms() {
    for (const core::Term& term : terms_.terms()) {
      if (term.kind == core::TermKind::Constant || term.kind == core::TermKind::Application) {
        closure_.add_term(term.id);
      }
    }
  }

  void add_assignment(const TheoryModelAssignment& assignment) {
    const core::Term& term = terms_.get(assignment.term);
    if (term.kind == core::TermKind::Equality) {
      if (assignment.value) {
        closure_.assert_equal(term.children.at(0), term.children.at(1));
      }
      return;
    }
    if (term.sort == terms_.symbols().bool_sort() &&
        (term.kind == core::TermKind::Constant || term.kind == core::TermKind::Application)) {
      closure_.assert_equal(assignment.term, assignment.value ? true_term_ : false_term_);
    }
  }

  void seed_defaults() {
    for (const core::Symbol& symbol : terms_.symbols().symbols()) {
      if (symbol.kind == core::SymbolKind::Constant &&
          symbol.result_sort != terms_.symbols().bool_sort()) {
        (void)value_name(closure_.class_representative(terms_.mk_constant(symbol.id)));
      }
    }
    for (const core::Term& term : terms_.terms()) {
      if ((term.kind == core::TermKind::Constant || term.kind == core::TermKind::Application) &&
          term.sort != terms_.symbols().bool_sort()) {
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

  std::string term_value(core::TermId term) {
    const core::Term& node = terms_.get(term);
    if (node.sort == terms_.symbols().bool_sort()) {
      return bool_value(term) ? "true" : "false";
    }
    return value_name(closure_.class_representative(term));
  }

  std::string constant_value(const core::Symbol& symbol) {
    const core::TermId term = terms_.mk_constant(symbol.id);
    return term_value(term);
  }

  std::vector<TableEntry> table_entries(const core::Symbol& symbol) {
    std::unordered_map<VectorKey, std::string, VectorKeyHash> seen;
    std::vector<TableEntry> entries;
    for (const core::Term& term : terms_.terms()) {
      if (term.kind != core::TermKind::Application || term.symbol != symbol.id) {
        continue;
      }
      VectorKey key;
      key.values.reserve(term.children.size());
      for (const core::TermId child : term.children) {
        key.values.push_back(term_value(child));
      }
      const std::string result = term_value(term.id);
      if (seen.emplace(key, result).second) {
        entries.push_back(TableEntry{key.values, result});
      }
    }
    std::sort(entries.begin(), entries.end(),
              [](const TableEntry& lhs, const TableEntry& rhs) { return lhs.args < rhs.args; });
    return entries;
  }

  std::string condition_for_entry(const TableEntry& entry) const {
    if (entry.args.empty()) {
      return "true";
    }
    if (entry.args.size() == 1) {
      return "(= x0 " + entry.args.front() + ")";
    }
    std::ostringstream out;
    out << "(and";
    for (std::size_t i = 0; i < entry.args.size(); ++i) {
      out << " (= x" << i << " " << entry.args[i] << ")";
    }
    out << ")";
    return out.str();
  }

  std::string default_result(const core::Symbol& symbol) {
    if (symbol.result_sort == terms_.symbols().bool_sort()) {
      return "false";
    }
    return default_value(symbol.result_sort);
  }

  std::string function_body(const core::Symbol& symbol) {
    std::string body = default_result(symbol);
    std::vector<TableEntry> entries = table_entries(symbol);
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
      if (it->result == body) {
        continue;
      }
      body = "(ite " + condition_for_entry(*it) + " " + it->result + " " + body + ")";
    }
    return body;
  }

  core::TermManager& terms_;
  theory::equality::IncrementalCongruenceClosure closure_;
  core::TermId true_term_ = core::kInvalidTermId;
  core::TermId false_term_ = core::kInvalidTermId;
  std::unordered_map<core::SortId, std::unordered_map<core::TermId, std::string>> values_by_sort_;
  std::unordered_map<core::SortId, std::string> default_value_by_sort_;
  std::unordered_map<core::SortId, std::size_t> next_value_index_by_sort_;
};

}  // namespace

std::string build_qf_uf_model(core::TermManager& terms,
                              const std::vector<TheoryModelAssignment>& theory_assignments) {
  QfUfModelBuilder builder(terms, theory_assignments);
  return builder.build();
}

}  // namespace primo::smt
