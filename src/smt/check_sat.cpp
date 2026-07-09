#include "smt/check_sat.hpp"

#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "sat/cadical_backend.hpp"
#include "sat/clause.hpp"
#include "sat/ipasir_up.hpp"
#include "sat/lit.hpp"
#include "smt/preprocessing.hpp"
#include "smtlib/ast.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/parser.hpp"
#include "theory/equality/congruence_closure.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

namespace {

constexpr std::string_view kStatAssertionsInitial = ":input.assertions";
constexpr std::string_view kStatFrontendBooleanItes = ":frontend.ite.boolean";
constexpr std::string_view kStatFrontendAuxiliaryItes = ":frontend.ite.auxiliaries";
constexpr std::string_view kStatAssertionsFinal = ":preprocessing.assertions";
constexpr std::string_view kStatPreprocessingPasses = ":preprocessing.passes";
constexpr std::string_view kStatPreprocessingChangedPasses = ":preprocessing.changed-passes";
constexpr std::string_view kStatEqBridgeEqualities = ":preprocessing.eq-bridge.equalities";
constexpr std::string_view kStatEqBridgeSkippedOrNodes =
    ":preprocessing.eq-bridge.skipped-or-nodes";
constexpr std::string_view kStatUnitEqualityConsequences =
    ":preprocessing.unit-equality.consequences";
constexpr std::string_view kStatUfSymmetryAcceptedSets = ":preprocessing.uf-symmetry.accepted-sets";
constexpr std::string_view kStatUfSymmetryConstraints = ":preprocessing.uf-symmetry.constraints";
constexpr std::string_view kStatUfSymmetryRejectedNoninvariant =
    ":preprocessing.uf-symmetry.rejected-noninvariant";
constexpr std::string_view kStatSolverBoolean = ":solver.boolean";
constexpr std::string_view kStatSolverQfUf = ":solver.qf-uf";
constexpr std::string_view kStatSatSolveCalls = ":sat.solve-calls";
constexpr std::string_view kStatTheoryAtoms = ":euf.theory-atoms";
constexpr std::string_view kStatTheoryChecks = ":euf.checks";
constexpr std::string_view kStatTheoryConflicts = ":euf.theory-conflicts";

class UnsupportedInput : public std::runtime_error {
 public:
  explicit UnsupportedInput(const std::string& message) : std::runtime_error(message) {}
};

class Frontend {
 public:
  core::TermId build_term(const smtlib::SExpr& expr) {
    if (expr.kind == smtlib::SExpr::Kind::Atom) {
      if (expr.atom == "true") {
        return terms_.mk_bool(true);
      }
      if (expr.atom == "false") {
        return terms_.mk_bool(false);
      }
      const auto bound = lookup_bound_term(expr.atom);
      if (bound.has_value()) {
        return bound.value();
      }
      const auto symbol = symbols_.lookup_symbol(expr.atom);
      if (!symbol.has_value()) {
        throw UnsupportedInput("unknown symbol: " + expr.atom);
      }
      return terms_.mk_constant(symbol.value());
    }

    if (expr.list.empty() || expr.list[0].kind != smtlib::SExpr::Kind::Atom) {
      throw UnsupportedInput("unsupported expression shape");
    }

    const std::string& head = expr.list[0].atom;
    if (head == "not") {
      require_arity(expr, 2, "not");
      return terms_.mk_not(build_term(expr.list[1]));
    }
    if (head == "and") {
      std::vector<core::TermId> children;
      for (std::size_t i = 1; i < expr.list.size(); ++i) {
        children.push_back(build_term(expr.list[i]));
      }
      return terms_.mk_and(std::move(children));
    }
    if (head == "or") {
      std::vector<core::TermId> children;
      for (std::size_t i = 1; i < expr.list.size(); ++i) {
        children.push_back(build_term(expr.list[i]));
      }
      return terms_.mk_or(std::move(children));
    }
    if (head == "=>") {
      return build_implies(expr);
    }
    if (head == "xor") {
      return build_xor(expr);
    }
    if (head == "!") {
      if (expr.list.size() < 2) {
        throw UnsupportedInput("annotated term has no body");
      }
      return build_term(expr.list[1]);
    }
    if (head == "ite") {
      return build_ite(expr);
    }
    if (head == "let") {
      return build_let(expr);
    }
    if (head == "=") {
      return build_equality(expr);
    }
    if (head == "distinct") {
      return build_distinct(expr);
    }

    const auto symbol = symbols_.lookup_symbol(head);
    if (!symbol.has_value()) {
      throw UnsupportedInput("unknown function symbol: " + head);
    }
    std::vector<core::TermId> args;
    for (std::size_t i = 1; i < expr.list.size(); ++i) {
      args.push_back(build_term(expr.list[i]));
    }
    const core::TermId term = terms_.mk_app(symbol.value(), std::move(args));
    return term;
  }

  void process_command(const smtlib::Command& command, std::vector<core::TermId>& assertions) {
    switch (command.kind) {
      case smtlib::CommandKind::Ignored:
        return;
      case smtlib::CommandKind::SetLogic:
        if (command.logic != "QF_UF") {
          throw UnsupportedInput("unsupported logic: " + command.logic);
        }
        return;
      case smtlib::CommandKind::DeclareSort:
        symbols_.declare_sort(command.name);
        return;
      case smtlib::CommandKind::DeclareFun:
        declare_fun(command);
        return;
      case smtlib::CommandKind::Assert:
        side_assertions_.clear();
        {
          const core::TermId assertion = build_term(command.assertion);
          assertions.insert(assertions.end(), side_assertions_.begin(), side_assertions_.end());
          assertions.push_back(assertion);
        }
        side_assertions_.clear();
        return;
      case smtlib::CommandKind::CheckSat:
        saw_check_sat_ = true;
        return;
    }
  }

  bool saw_check_sat() const { return saw_check_sat_; }
  const core::SymbolTable& symbols() const { return symbols_; }
  const core::TermManager& terms() const { return terms_; }
  core::TermManager& terms() { return terms_; }
  std::size_t boolean_ites() const { return boolean_ites_; }
  std::size_t auxiliary_ites() const { return auxiliary_ites_; }

 private:
  std::optional<core::TermId> lookup_bound_term(const std::string& name) const {
    for (auto scope = let_scopes_.rbegin(); scope != let_scopes_.rend(); ++scope) {
      const auto found = scope->find(name);
      if (found != scope->end()) {
        return found->second;
      }
    }
    return std::nullopt;
  }

  static void require_arity(const smtlib::SExpr& expr, std::size_t arity, std::string_view op) {
    if (expr.list.size() != arity) {
      throw UnsupportedInput(std::string(op) + " has wrong arity");
    }
  }

  core::TermId build_let(const smtlib::SExpr& expr) {
    require_arity(expr, 3, "let");
    if (expr.list[1].kind != smtlib::SExpr::Kind::List) {
      throw UnsupportedInput("let bindings must be a list");
    }

    std::unordered_map<std::string, core::TermId> scope;
    for (const smtlib::SExpr& binding : expr.list[1].list) {
      if (binding.kind != smtlib::SExpr::Kind::List || binding.list.size() != 2 ||
          binding.list[0].kind != smtlib::SExpr::Kind::Atom) {
        throw UnsupportedInput("malformed let binding");
      }
      const std::string& name = binding.list[0].atom;
      if (!scope.emplace(name, build_term(binding.list[1])).second) {
        throw UnsupportedInput("duplicate let binding: " + name);
      }
    }

    let_scopes_.push_back(std::move(scope));
    const core::TermId body = build_term(expr.list[2]);
    let_scopes_.pop_back();
    return body;
  }

  core::TermId build_ite(const smtlib::SExpr& expr) {
    require_arity(expr, 4, "ite");
    const core::TermId condition = build_term(expr.list[1]);
    if (terms_.get(condition).sort != symbols_.bool_sort()) {
      throw UnsupportedInput("ite condition is not Boolean");
    }
    const core::TermId then_term = build_term(expr.list[2]);
    const core::TermId else_term = build_term(expr.list[3]);
    const core::SortId result_sort = terms_.get(then_term).sort;
    if (result_sort != terms_.get(else_term).sort) {
      throw UnsupportedInput("ite branch sort mismatch");
    }
    if (result_sort == symbols_.bool_sort()) {
      ++boolean_ites_;
      return terms_.mk_or({terms_.mk_and({condition, then_term}),
                           terms_.mk_and({terms_.mk_not(condition), else_term})});
    }

    const core::TermId auxiliary = fresh_auxiliary_constant(result_sort, "ite");
    ++auxiliary_ites_;
    side_assertions_.push_back(terms_.mk_implies(condition, terms_.mk_eq(auxiliary, then_term)));
    side_assertions_.push_back(
        terms_.mk_implies(terms_.mk_not(condition), terms_.mk_eq(auxiliary, else_term)));
    return auxiliary;
  }

  core::TermId build_implies(const smtlib::SExpr& expr) {
    if (expr.list.size() < 3) {
      throw UnsupportedInput("=> has wrong arity");
    }
    core::TermId result = build_term(expr.list.back());
    for (std::size_t i = expr.list.size() - 2; i >= 1; --i) {
      result = terms_.mk_implies(build_term(expr.list[i]), result);
      if (i == 1) {
        break;
      }
    }
    return result;
  }

  core::TermId build_xor(const smtlib::SExpr& expr) {
    if (expr.list.size() < 3) {
      throw UnsupportedInput("xor has wrong arity");
    }
    core::TermId result = build_term(expr.list[1]);
    for (std::size_t i = 2; i < expr.list.size(); ++i) {
      const core::TermId rhs = build_term(expr.list[i]);
      result = terms_.mk_or({terms_.mk_and({result, terms_.mk_not(rhs)}),
                             terms_.mk_and({terms_.mk_not(result), rhs})});
    }
    return result;
  }

  core::TermId build_equality(const smtlib::SExpr& expr) {
    if (expr.list.size() < 3) {
      throw UnsupportedInput("= has wrong arity");
    }
    std::vector<core::TermId> equalities;
    core::TermId previous = build_term(expr.list[1]);
    for (std::size_t i = 2; i < expr.list.size(); ++i) {
      const core::TermId current = build_term(expr.list[i]);
      equalities.push_back(terms_.mk_eq(previous, current));
      previous = current;
    }
    return terms_.mk_and(std::move(equalities));
  }

  core::TermId build_distinct(const smtlib::SExpr& expr) {
    std::vector<core::TermId> terms;
    for (std::size_t i = 1; i < expr.list.size(); ++i) {
      terms.push_back(build_term(expr.list[i]));
    }
    std::vector<core::TermId> disequalities;
    for (std::size_t i = 0; i < terms.size(); ++i) {
      for (std::size_t j = i + 1; j < terms.size(); ++j) {
        disequalities.push_back(terms_.mk_not(terms_.mk_eq(terms[i], terms[j])));
      }
    }
    if (disequalities.empty()) {
      return terms_.mk_bool(true);
    }
    if (disequalities.size() == 1) {
      return disequalities.front();
    }
    return terms_.mk_and(std::move(disequalities));
  }

  core::SortId lookup_sort_name(const std::string& name) const {
    const auto sort = symbols_.lookup_sort(name);
    if (!sort.has_value()) {
      throw UnsupportedInput("unknown sort: " + name);
    }
    return sort.value();
  }

  void declare_fun(const smtlib::Command& command) {
    std::vector<core::SortId> arg_sorts;
    for (const std::string& name : command.arg_sorts) {
      arg_sorts.push_back(lookup_sort_name(name));
    }
    const core::SortId result_sort = lookup_sort_name(command.result_sort);
    if (arg_sorts.empty()) {
      symbols_.declare_constant(command.name, result_sort);
    } else if (result_sort == symbols_.bool_sort()) {
      symbols_.declare_predicate(command.name, std::move(arg_sorts));
    } else {
      symbols_.declare_function(command.name, std::move(arg_sorts), result_sort);
    }
  }

  core::TermId fresh_auxiliary_constant(core::SortId sort, std::string_view prefix) {
    while (true) {
      const std::string name =
          std::string{"$primo_"} + std::string{prefix} + "_" + std::to_string(auxiliary_counter_++);
      if (symbols_.lookup_symbol(name).has_value()) {
        continue;
      }
      return terms_.mk_constant(symbols_.declare_constant(name, sort));
    }
  }

  core::SymbolTable symbols_;
  core::TermManager terms_{symbols_};
  std::vector<std::unordered_map<std::string, core::TermId>> let_scopes_;
  std::vector<core::TermId> side_assertions_;
  std::size_t auxiliary_counter_ = 0;
  std::size_t boolean_ites_ = 0;
  std::size_t auxiliary_ites_ = 0;
  bool saw_check_sat_ = false;
};

class BooleanClausifier {
 public:
  struct TheoryAtom {
    sat::Lit lit;
    core::TermId term;
  };

  explicit BooleanClausifier(sat::IpasirUpBackend& backend, const core::TermManager& terms)
      : backend_(backend), terms_(terms) {}

  void assert_formula(core::TermId term) {
    if (terms_.get(term).sort != terms_.symbols().bool_sort()) {
      throw UnsupportedInput("assertion is not Boolean");
    }
    const sat::Lit lit = encode(term);
    backend_.add_clause(sat::Clause{lit});
  }

  const std::vector<TheoryAtom>& theory_atoms() const { return theory_atoms_; }

 private:
  sat::Lit encode(core::TermId term_id) {
    const auto existing = lit_by_term_.find(term_id);
    if (existing != lit_by_term_.end()) {
      return existing->second;
    }

    const core::Term& term = terms_.get(term_id);
    if (term.kind == core::TermKind::BoolValue) {
      return term.bool_value ? true_lit() : negate(true_lit());
    }

    if (term.kind == core::TermKind::Constant) {
      return atom_lit(term_id);
    }

    if (term.kind == core::TermKind::Not) {
      const sat::Lit lit = negate(encode(term.children.at(0)));
      lit_by_term_.emplace(term_id, lit);
      return lit;
    }

    const sat::Lit lit{backend_.new_var()};
    lit_by_term_.emplace(term_id, lit);

    switch (term.kind) {
      case core::TermKind::And:
        encode_and(lit, term.children);
        return lit;
      case core::TermKind::Or:
        encode_or(lit, term.children);
        return lit;
      case core::TermKind::Implies:
        encode_implies(lit, encode(term.children.at(0)), encode(term.children.at(1)));
        return lit;
      case core::TermKind::Application:
        if (term.sort != terms_.symbols().bool_sort()) {
          throw UnsupportedInput("non-Boolean function application in Boolean context");
        }
        theory_atoms_.push_back(TheoryAtom{lit, term_id});
        return lit;
      case core::TermKind::Equality:
        theory_atoms_.push_back(TheoryAtom{lit, term_id});
        return lit;
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Not:
        break;
    }
    throw UnsupportedInput("unsupported Boolean term");
  }

  sat::Lit atom_lit(core::TermId term_id) {
    const auto existing = lit_by_term_.find(term_id);
    if (existing != lit_by_term_.end()) {
      return existing->second;
    }
    const sat::Lit lit{backend_.new_var()};
    lit_by_term_.emplace(term_id, lit);
    return lit;
  }

  sat::Lit true_lit() {
    if (true_lit_.value == 0) {
      true_lit_ = sat::Lit{backend_.new_var()};
      backend_.add_clause(sat::Clause{true_lit_});
    }
    return true_lit_;
  }

  static sat::Lit negate(sat::Lit lit) { return sat::Lit{-lit.value}; }

  void encode_and(sat::Lit output, const std::vector<core::TermId>& children) {
    sat::Clause reverse_clause{output};
    for (const core::TermId child : children) {
      const sat::Lit child_lit = encode(child);
      backend_.add_clause(sat::Clause{negate(output), child_lit});
      reverse_clause.push_back(negate(child_lit));
    }
    backend_.add_clause(reverse_clause);
  }

  void encode_or(sat::Lit output, const std::vector<core::TermId>& children) {
    sat::Clause forward_clause{negate(output)};
    for (const core::TermId child : children) {
      const sat::Lit child_lit = encode(child);
      backend_.add_clause(sat::Clause{negate(child_lit), output});
      forward_clause.push_back(child_lit);
    }
    backend_.add_clause(forward_clause);
  }

  void encode_implies(sat::Lit output, sat::Lit lhs, sat::Lit rhs) {
    backend_.add_clause(sat::Clause{lhs, output});
    backend_.add_clause(sat::Clause{negate(rhs), output});
    backend_.add_clause(sat::Clause{negate(output), negate(lhs), rhs});
  }

  sat::IpasirUpBackend& backend_;
  const core::TermManager& terms_;
  std::unordered_map<core::TermId, sat::Lit> lit_by_term_;
  std::vector<TheoryAtom> theory_atoms_;
  sat::Lit true_lit_{0};
};

CheckSatResult map_sat_result(sat::SatResult result) {
  switch (result) {
    case sat::SatResult::Sat:
      return CheckSatResult::Sat;
    case sat::SatResult::Unsat:
      return CheckSatResult::Unsat;
    case sat::SatResult::Unknown:
      return CheckSatResult::Unknown;
    case sat::SatResult::Unsupported:
      return CheckSatResult::Unsupported;
  }
  return CheckSatResult::Unknown;
}

bool contains_equality(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Equality) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_equality(terms, child)) {
      return true;
    }
  }
  return false;
}

CheckSatOutcome solve_boolean_conjunction(const core::TermManager& terms,
                                          const std::vector<core::TermId>& assertions,
                                          util::Statistics& statistics) {
  statistics.increment(kStatSolverBoolean);
  sat::CadicalBackend backend;
  BooleanClausifier clausifier(backend, terms);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }

  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  statistics.increment(kStatSatSolveCalls);
  return CheckSatOutcome{map_sat_result(backend.solve()), "", statistics};
}

struct AssignedTheoryAtom {
  int var = 0;
  bool value = false;
  core::TermId term = 0;
};

sat::Lit blocking_lit(const AssignedTheoryAtom& atom) {
  return atom.value ? sat::Lit{-atom.var} : sat::Lit{atom.var};
}

bool add_theory_equality(theory::equality::CongruenceClosure& closure,
                         const core::TermManager& terms, const AssignedTheoryAtom& atom,
                         core::TermId true_term, core::TermId false_term,
                         sat::Clause& equality_reason) {
  const core::Term& term = terms.get(atom.term);
  if (term.kind == core::TermKind::Equality && atom.value) {
    closure.assert_equal(term.children.at(0), term.children.at(1));
    equality_reason.push_back(blocking_lit(atom));
    return true;
  }
  if (term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort()) {
    closure.assert_equal(atom.term, atom.value ? true_term : false_term);
    equality_reason.push_back(blocking_lit(atom));
    return true;
  }
  return false;
}

std::optional<sat::Clause> find_theory_conflict(const core::TermManager& terms,
                                                const std::vector<AssignedTheoryAtom>& atoms,
                                                core::TermId true_term, core::TermId false_term) {
  theory::equality::CongruenceClosure closure(terms);
  sat::Clause equality_reason;
  std::vector<AssignedTheoryAtom> disequalities;

  closure.add_term(true_term);
  closure.add_term(false_term);
  for (const AssignedTheoryAtom& atom : atoms) {
    const core::Term& term = terms.get(atom.term);
    if (term.kind == core::TermKind::Equality && !atom.value) {
      disequalities.push_back(atom);
      continue;
    }
    if (!add_theory_equality(closure, terms, atom, true_term, false_term, equality_reason)) {
      throw UnsupportedInput("unsupported theory atom");
    }
  }

  if (closure.are_equal(true_term, false_term)) {
    return equality_reason;
  }

  for (const AssignedTheoryAtom& atom : disequalities) {
    const core::Term& term = terms.get(atom.term);
    if (closure.are_equal(term.children.at(0), term.children.at(1))) {
      sat::Clause conflict = equality_reason;
      conflict.push_back(blocking_lit(atom));
      return conflict;
    }
  }

  return std::nullopt;
}

CheckSatOutcome solve_qf_uf(core::TermManager& terms, const std::vector<core::TermId>& assertions,
                            util::Statistics& statistics) {
  statistics.increment(kStatSolverQfUf);
  sat::CadicalBackend backend;
  BooleanClausifier clausifier(backend, terms);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());

  const core::TermId true_term = terms.mk_bool(true);
  const core::TermId false_term = terms.mk_bool(false);

  while (true) {
    statistics.increment(kStatSatSolveCalls);
    const sat::SatResult sat_result = backend.solve();
    if (sat_result != sat::SatResult::Sat) {
      return CheckSatOutcome{map_sat_result(sat_result), "", statistics};
    }
    statistics.increment(kStatTheoryChecks);

    std::vector<AssignedTheoryAtom> assigned_atoms;
    for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      const std::optional<bool> assignment = backend.value(var);
      if (!assignment.has_value()) {
        return CheckSatOutcome{CheckSatResult::Unknown, "SAT backend did not assign theory atom",
                               statistics};
      }
      assigned_atoms.push_back(AssignedTheoryAtom{var, assignment.value(), atom.term});
    }

    std::optional<sat::Clause> conflict_clause =
        find_theory_conflict(terms, assigned_atoms, true_term, false_term);
    if (!conflict_clause.has_value()) {
      return CheckSatOutcome{CheckSatResult::Sat, "", statistics};
    }
    statistics.increment(kStatTheoryConflicts);
    if (conflict_clause->empty()) {
      return CheckSatOutcome{CheckSatResult::Unsat, "", statistics};
    }
    backend.add_clause(conflict_clause.value());
  }
}

}  // namespace

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options) {
  util::Statistics statistics;
  statistics.set(kStatAssertionsInitial, 0);
  statistics.set(kStatFrontendBooleanItes, 0);
  statistics.set(kStatFrontendAuxiliaryItes, 0);
  statistics.set(kStatAssertionsFinal, 0);
  statistics.set(kStatPreprocessingPasses, 0);
  statistics.set(kStatPreprocessingChangedPasses, 0);
  statistics.set(kStatEqBridgeEqualities, 0);
  statistics.set(kStatEqBridgeSkippedOrNodes, 0);
  statistics.set(kStatUnitEqualityConsequences, 0);
  statistics.set(kStatUfSymmetryAcceptedSets, 0);
  statistics.set(kStatUfSymmetryConstraints, 0);
  statistics.set(kStatUfSymmetryRejectedNoninvariant, 0);
  statistics.set(kStatSolverBoolean, 0);
  statistics.set(kStatSolverQfUf, 0);
  statistics.set(kStatSatSolveCalls, 0);
  statistics.set(kStatTheoryAtoms, 0);
  statistics.set(kStatTheoryChecks, 0);
  statistics.set(kStatTheoryConflicts, 0);

  try {
    smtlib::Parser parser(input);
    const std::vector<smtlib::Command> commands = parser.parse_commands();

    Frontend frontend;
    std::vector<core::TermId> assertions;
    for (const smtlib::Command& command : commands) {
      frontend.process_command(command, assertions);
    }
    statistics.set(kStatAssertionsInitial, assertions.size());
    statistics.set(kStatFrontendBooleanItes, frontend.boolean_ites());
    statistics.set(kStatFrontendAuxiliaryItes, frontend.auxiliary_ites());
    if (!frontend.saw_check_sat()) {
      return CheckSatOutcome{CheckSatResult::Unsupported, "missing check-sat command", statistics};
    }

    if (options.uf_symmetry_breaking) {
      UfSymmetryBreaker symmetry_breaker(frontend.terms());
      UfSymmetryBreakingResult symmetry = symmetry_breaker.break_symmetries(assertions);
      assertions.insert(assertions.end(), symmetry.constraints.begin(), symmetry.constraints.end());
      statistics.increment(kStatUfSymmetryAcceptedSets, symmetry.accepted_sets);
      statistics.increment(kStatUfSymmetryConstraints, symmetry.constraints.size());
      statistics.increment(kStatUfSymmetryRejectedNoninvariant, symmetry.rejected_noninvariant);
    }
    for (std::size_t pass = 0; pass < options.preprocessing_passes; ++pass) {
      const std::vector<core::TermId> before = assertions;
      statistics.increment(kStatPreprocessingPasses);
      if (options.eq_bridge) {
        EqualityBridge bridge(frontend.terms());
        EqualityBridgeResult bridge_result = bridge.bridge(assertions);
        assertions.insert(assertions.end(), bridge_result.equalities.begin(),
                          bridge_result.equalities.end());
        statistics.increment(kStatEqBridgeEqualities, bridge_result.equalities.size());
        statistics.increment(kStatEqBridgeSkippedOrNodes, bridge_result.skipped_or_nodes);
      }
      std::size_t unit_consequences = 0;
      assertions = propagate_unit_equalities(frontend.terms(), assertions, &unit_consequences);
      statistics.increment(kStatUnitEqualityConsequences, unit_consequences);
      if (assertions == before) {
        break;
      }
      statistics.increment(kStatPreprocessingChangedPasses);
    }
    statistics.set(kStatAssertionsFinal, assertions.size());

    const bool has_equality = [&] {
      for (const core::TermId assertion : assertions) {
        if (contains_equality(frontend.terms(), assertion)) {
          return true;
        }
      }
      return false;
    }();
    if (has_equality) {
      return solve_qf_uf(frontend.terms(), assertions, statistics);
    }
    try {
      return solve_boolean_conjunction(frontend.terms(), assertions, statistics);
    } catch (const UnsupportedInput&) {
      return solve_qf_uf(frontend.terms(), assertions, statistics);
    }
  } catch (const UnsupportedInput& error) {
    return CheckSatOutcome{CheckSatResult::Unsupported, error.what(), statistics};
  } catch (const smtlib::ParseError& error) {
    return CheckSatOutcome{CheckSatResult::Unsupported, error.what(), statistics};
  } catch (const std::exception& error) {
    return CheckSatOutcome{CheckSatResult::Unsupported, error.what(), statistics};
  }
}

std::string_view to_string(CheckSatResult result) {
  switch (result) {
    case CheckSatResult::Sat:
      return "sat";
    case CheckSatResult::Unsat:
      return "unsat";
    case CheckSatResult::Unknown:
      return "unknown";
    case CheckSatResult::Unsupported:
      return "unsupported";
  }
  return "unknown";
}

}  // namespace primo::smt
