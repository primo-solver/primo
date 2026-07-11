#include "smt/check_sat.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
#include "theory/equality/congruence_stats.hpp"
#include "theory/equality/explained_congruence_closure.hpp"
#include "theory/equality/flat_congruence_closure.hpp"
#include "theory/equality/flat_terms.hpp"
#include "util/statistics.hpp"

#if PRIMO_HAVE_IPASIR
#include <cadical.hpp>
#endif

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
constexpr std::string_view kStatFiniteDomainDomains = ":preprocessing.finite-domain.domains";
constexpr std::string_view kStatFiniteDomainAmoClauses = ":preprocessing.finite-domain.amo-clauses";
constexpr std::string_view kStatFiniteDomainEvaluatedEqualities =
    ":preprocessing.finite-domain.evaluated-equalities";
constexpr std::string_view kStatFiniteDomainEvaluationCutoffs =
    ":preprocessing.finite-domain.evaluation-cutoffs";
constexpr std::string_view kStatEqualityDefinitions = ":preprocessing.equality-definitions";
constexpr std::string_view kStatGroundCongruenceClauses =
    ":preprocessing.ground-congruence-clauses";
constexpr std::string_view kStatGroundCongruenceSkippedGroups =
    ":preprocessing.ground-congruence-skipped-groups";
constexpr std::string_view kStatUfSymmetryAcceptedSets = ":preprocessing.uf-symmetry.accepted-sets";
constexpr std::string_view kStatUfSymmetryConstraints = ":preprocessing.uf-symmetry.constraints";
constexpr std::string_view kStatUfSymmetryRejectedNoninvariant =
    ":preprocessing.uf-symmetry.rejected-noninvariant";
constexpr std::string_view kStatAutoSingleAssertion = ":preprocessing.auto.single-assertion";
constexpr std::string_view kStatAutoStrong = ":preprocessing.auto.strong";
constexpr std::string_view kStatSatTheoryNegativePhase = ":sat.phase.theory-negative";
constexpr std::string_view kStatSolverBoolean = ":solver.boolean";
constexpr std::string_view kStatSolverQfUf = ":solver.qf-uf";
constexpr std::string_view kStatSatSolveCalls = ":sat.solve-calls";
constexpr std::string_view kStatTheoryAtoms = ":euf.theory-atoms";
constexpr std::string_view kStatTheoryChecks = ":euf.checks";
constexpr std::string_view kStatTheoryConflicts = ":euf.theory-conflicts";
constexpr std::string_view kStatEufPropagationRounds = ":euf.propagation.rounds";
constexpr std::string_view kStatEufPropagationCandidates = ":euf.propagation.candidates";
constexpr std::string_view kStatEufPropagationDelivered = ":euf.propagation.delivered";
constexpr std::string_view kStatEufPropagationReasonLits = ":euf.propagation.reason-lits";
constexpr std::string_view kStatEufPropagationBudget = ":euf.propagation.budget";
constexpr std::string_view kStatEufPropagationDeliveryBudget = ":euf.propagation.delivery-budget";
constexpr std::string_view kStatEufPropagationBudgetCutoffs = ":euf.propagation.budget-cutoffs";
constexpr std::string_view kStatEufPropagationHitRateCutoffs = ":euf.propagation.hit-rate-cutoffs";
constexpr std::string_view kStatEufIncrementalTerms = ":euf.incremental.terms";
constexpr std::string_view kStatEufIncrementalFlatDefinitions = ":euf.incremental.flat-definitions";
constexpr std::string_view kStatEufIncrementalMerges = ":euf.incremental.merges";
constexpr std::string_view kStatEufIncrementalPendingPops = ":euf.incremental.pending-pops";
constexpr std::string_view kStatEufIncrementalLookupProbes = ":euf.incremental.lookup-probes";
constexpr std::string_view kStatEufIncrementalLookupHits = ":euf.incremental.lookup-hits";
constexpr std::string_view kStatEufIncrementalCongruenceMerges =
    ":euf.incremental.congruence-merges";
constexpr std::string_view kStatEufIncrementalDisequalityChecks =
    ":euf.incremental.disequality-checks";
constexpr std::string_view kStatEufIncrementalConflicts = ":euf.incremental.conflicts";
constexpr std::string_view kStatEufIncrementalExplainLength = ":euf.incremental.explain-length";
constexpr std::string_view kStatEufIncrementalBacktracks = ":euf.incremental.backtracks";

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
      const auto defined = definitions_.find(expr.atom);
      if (defined != definitions_.end() && defined->second.parameters.empty()) {
        return instantiate_definition(expr.atom, {});
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

    const auto defined = definitions_.find(head);
    if (defined != definitions_.end()) {
      std::vector<core::TermId> args;
      for (std::size_t i = 1; i < expr.list.size(); ++i) {
        args.push_back(build_term(expr.list[i]));
      }
      return instantiate_definition(head, std::move(args));
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
      case smtlib::CommandKind::DefineFun:
        define_fun(command);
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
  struct Definition {
    std::vector<std::string> parameters;
    std::vector<core::SortId> parameter_sorts;
    core::SortId result_sort = core::kInvalidSortId;
    smtlib::SExpr body;
  };

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
      equalities.push_back(build_pairwise_equality(previous, current));
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
        disequalities.push_back(build_pairwise_disequality(terms[i], terms[j]));
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

  core::TermId build_pairwise_equality(core::TermId lhs, core::TermId rhs) {
    const core::SortId lhs_sort = terms_.get(lhs).sort;
    if (lhs_sort != terms_.get(rhs).sort) {
      throw UnsupportedInput("equality sort mismatch");
    }
    if (lhs_sort == symbols_.bool_sort()) {
      return terms_.mk_and({terms_.mk_implies(lhs, rhs), terms_.mk_implies(rhs, lhs)});
    }
    return terms_.mk_eq(lhs, rhs);
  }

  core::TermId build_pairwise_disequality(core::TermId lhs, core::TermId rhs) {
    const core::SortId lhs_sort = terms_.get(lhs).sort;
    if (lhs_sort != terms_.get(rhs).sort) {
      throw UnsupportedInput("distinct sort mismatch");
    }
    if (lhs_sort == symbols_.bool_sort()) {
      return terms_.mk_or(
          {terms_.mk_and({lhs, terms_.mk_not(rhs)}), terms_.mk_and({terms_.mk_not(lhs), rhs})});
    }
    return terms_.mk_not(terms_.mk_eq(lhs, rhs));
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

  void define_fun(const smtlib::Command& command) {
    if (definitions_.contains(command.name) || symbols_.lookup_symbol(command.name).has_value()) {
      throw UnsupportedInput("duplicate function definition: " + command.name);
    }
    Definition definition;
    definition.parameters.reserve(command.parameters.size());
    definition.parameter_sorts.reserve(command.parameters.size());
    for (const smtlib::SortedVar& parameter : command.parameters) {
      definition.parameters.push_back(parameter.name);
      definition.parameter_sorts.push_back(lookup_sort_name(parameter.sort));
    }
    definition.result_sort = lookup_sort_name(command.result_sort);
    definition.body = command.body;
    definitions_.emplace(command.name, std::move(definition));
  }

  core::TermId instantiate_definition(const std::string& name, std::vector<core::TermId> args) {
    const auto found = definitions_.find(name);
    if (found == definitions_.end()) {
      throw UnsupportedInput("unknown definition: " + name);
    }
    if (expanding_definitions_.contains(name)) {
      throw UnsupportedInput("recursive define-fun is unsupported: " + name);
    }
    const Definition& definition = found->second;
    if (args.size() != definition.parameters.size()) {
      throw UnsupportedInput("define-fun application has wrong arity: " + name);
    }

    std::unordered_map<std::string, core::TermId> scope;
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (terms_.get(args[i]).sort != definition.parameter_sorts[i]) {
        throw UnsupportedInput("define-fun argument sort mismatch: " + name);
      }
      if (!scope.emplace(definition.parameters[i], args[i]).second) {
        throw UnsupportedInput("duplicate define-fun parameter: " + definition.parameters[i]);
      }
    }

    expanding_definitions_.insert(name);
    let_scopes_.push_back(std::move(scope));
    const core::TermId body = build_term(definition.body);
    let_scopes_.pop_back();
    expanding_definitions_.erase(name);
    if (terms_.get(body).sort != definition.result_sort) {
      throw UnsupportedInput("define-fun result sort mismatch: " + name);
    }
    return body;
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
  std::unordered_map<std::string, Definition> definitions_;
  std::unordered_set<std::string> expanding_definitions_;
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
    const core::Term& term = terms_.get(term_id);
    if (term.kind == core::TermKind::Constant && term.sort == terms_.symbols().bool_sort()) {
      theory_atoms_.push_back(TheoryAtom{lit, term_id});
    }
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
  int lit = 0;
  bool value = false;
  core::TermId term = 0;
};

sat::Lit blocking_lit(const AssignedTheoryAtom& atom) {
  return atom.value ? sat::Lit{-atom.lit} : sat::Lit{atom.lit};
}

sat::Lit assigned_lit(const AssignedTheoryAtom& atom) {
  return atom.value ? sat::Lit{atom.lit} : sat::Lit{-atom.lit};
}

bool add_explained_theory_equality(theory::equality::ExplainedCongruenceClosure& closure,
                                   const core::TermManager& terms, const AssignedTheoryAtom& atom,
                                   core::TermId true_term, core::TermId false_term) {
  const core::Term& term = terms.get(atom.term);
  if (term.kind == core::TermKind::Equality && atom.value) {
    closure.assert_equal(term.children.at(0), term.children.at(1), {blocking_lit(atom)});
    return true;
  }
  if (term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort()) {
    closure.assert_equal(atom.term, atom.value ? true_term : false_term, {blocking_lit(atom)});
    return true;
  }
  if (term.kind == core::TermKind::Constant && term.sort == terms.symbols().bool_sort()) {
    closure.assert_equal(atom.term, atom.value ? true_term : false_term, {blocking_lit(atom)});
    return true;
  }
  return false;
}

void add_incremental_stats(util::Statistics& statistics,
                           const theory::equality::IncrementalCongruenceClosureStats& stats) {
  statistics.increment(kStatEufIncrementalTerms, stats.terms);
  statistics.increment(kStatEufIncrementalFlatDefinitions, stats.flat_definitions);
  statistics.increment(kStatEufIncrementalMerges, stats.merges);
  statistics.increment(kStatEufIncrementalPendingPops, stats.pending_pops);
  statistics.increment(kStatEufIncrementalLookupProbes, stats.lookup_probes);
  statistics.increment(kStatEufIncrementalLookupHits, stats.lookup_hits);
  statistics.increment(kStatEufIncrementalCongruenceMerges, stats.congruence_merges);
  statistics.increment(kStatEufIncrementalDisequalityChecks, stats.disequality_checks);
  statistics.increment(kStatEufIncrementalConflicts, stats.conflicts);
  statistics.increment(kStatEufIncrementalExplainLength, stats.explain_length);
  statistics.increment(kStatEufIncrementalBacktracks, stats.backtracks);
}

std::size_t positive_delta(std::size_t current, std::size_t previous) {
  return current >= previous ? current - previous : 0;
}

theory::equality::IncrementalCongruenceClosureStats stats_delta(
    const theory::equality::IncrementalCongruenceClosureStats& lhs,
    const theory::equality::IncrementalCongruenceClosureStats& rhs) {
  return theory::equality::IncrementalCongruenceClosureStats{
      positive_delta(lhs.terms, rhs.terms),
      positive_delta(lhs.flat_definitions, rhs.flat_definitions),
      positive_delta(lhs.merges, rhs.merges),
      positive_delta(lhs.pending_pops, rhs.pending_pops),
      positive_delta(lhs.lookup_probes, rhs.lookup_probes),
      positive_delta(lhs.lookup_hits, rhs.lookup_hits),
      positive_delta(lhs.congruence_merges, rhs.congruence_merges),
      positive_delta(lhs.disequality_checks, rhs.disequality_checks),
      positive_delta(lhs.conflicts, rhs.conflicts),
      positive_delta(lhs.explain_length, rhs.explain_length),
      positive_delta(lhs.backtracks, rhs.backtracks),
  };
}

std::optional<std::size_t> resolve_budget(AutoBudget budget, std::size_t theory_atoms) {
  switch (budget.kind) {
    case AutoBudget::Kind::Auto:
      return theory_atoms;
    case AutoBudget::Kind::Unlimited:
      return std::nullopt;
    case AutoBudget::Kind::Fixed:
      return budget.value;
  }
  return theory_atoms;
}

std::optional<std::size_t> resolve_delivery_budget(AutoBudget budget, std::size_t candidate_budget,
                                                   std::size_t theory_atoms) {
  switch (budget.kind) {
    case AutoBudget::Kind::Auto:
      return std::max<std::size_t>(1, std::min(theory_atoms, candidate_budget / 2 + 1));
    case AutoBudget::Kind::Unlimited:
      return std::nullopt;
    case AutoBudget::Kind::Fixed:
      if (budget.value == 0) {
        return std::nullopt;
      }
      return budget.value;
  }
  return std::max<std::size_t>(1, std::min(theory_atoms, candidate_budget / 2 + 1));
}

std::string clause_key(const sat::Clause& clause) {
  std::string key;
  for (const sat::Lit lit : clause) {
    key += std::to_string(lit.value);
    key += ',';
  }
  return key;
}

std::optional<std::vector<sat::Lit>> candidate_explanation(
    theory::equality::ExplainedCongruenceClosure& closure, const core::TermManager& terms,
    const AssignedTheoryAtom& atom, core::TermId true_term, core::TermId false_term) {
  const core::Term& term = terms.get(atom.term);
  if (term.kind == core::TermKind::Equality) {
    if (!atom.value) {
      return std::nullopt;
    }
    return closure.explain_equal(term.children.at(0), term.children.at(1));
  }
  if (term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort()) {
    return closure.explain_equal(atom.term, atom.value ? true_term : false_term);
  }
  if (term.kind == core::TermKind::Constant && term.sort == terms.symbols().bool_sort()) {
    return closure.explain_equal(atom.term, atom.value ? true_term : false_term);
  }
  throw UnsupportedInput("unsupported theory atom");
}

std::optional<sat::Clause> model_propagation_clause_for(
    const core::TermManager& terms, const std::vector<AssignedTheoryAtom>& atoms,
    std::size_t candidate_index, core::TermId true_term, core::TermId false_term) {
  theory::equality::ExplainedCongruenceClosure explained(terms);
  explained.add_term(true_term);
  explained.add_term(false_term);

  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (i == candidate_index) {
      continue;
    }
    const AssignedTheoryAtom& atom = atoms[i];
    const core::Term& term = terms.get(atom.term);
    if (term.kind == core::TermKind::Equality && !atom.value) {
      continue;
    }
    if (!add_explained_theory_equality(explained, terms, atom, true_term, false_term)) {
      throw UnsupportedInput("unsupported theory atom");
    }
  }

  const AssignedTheoryAtom& candidate = atoms[candidate_index];
  std::optional<std::vector<sat::Lit>> reason =
      candidate_explanation(explained, terms, candidate, true_term, false_term);
  if (!reason.has_value()) {
    return std::nullopt;
  }
  reason->push_back(assigned_lit(candidate));
  return reason;
}

struct FlatAssignedTheoryAtom {
  AssignedTheoryAtom source;
  bool equality = false;
  theory::equality::FlatTermId lhs = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId rhs = theory::equality::kInvalidFlatTermId;
};

struct PreparedFlatTheory {
  PreparedFlatTheory(const core::TermManager& terms,
                     const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                     core::TermId true_term, core::TermId false_term);

  theory::equality::FlatTermIndex flat;
  theory::equality::FlatCongruenceClosure base_closure;
  theory::equality::FlatTermId true_flat = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId false_flat = theory::equality::kInvalidFlatTermId;
  std::vector<FlatAssignedTheoryAtom> flat_atoms;
};

FlatAssignedTheoryAtom flatten_assigned_atom(theory::equality::FlatTermIndex& flat,
                                             const core::TermManager& terms,
                                             const AssignedTheoryAtom& atom,
                                             theory::equality::FlatTermId false_flat) {
  const core::Term& term = terms.get(atom.term);
  if (term.kind == core::TermKind::Equality) {
    return FlatAssignedTheoryAtom{atom, true, flat.flatten(term.children.at(0)),
                                  flat.flatten(term.children.at(1))};
  }
  if (term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort()) {
    return FlatAssignedTheoryAtom{atom, false, flat.flatten(atom.term), false_flat};
  }
  if (term.kind == core::TermKind::Constant && term.sort == terms.symbols().bool_sort()) {
    return FlatAssignedTheoryAtom{atom, false, flat.flatten(atom.term), false_flat};
  }
  throw UnsupportedInput("unsupported theory atom");
}

PreparedFlatTheory::PreparedFlatTheory(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
    core::TermId true_term, core::TermId false_term)
    : flat(terms), base_closure(0) {
  true_flat = flat.flatten(true_term);
  false_flat = flat.flatten(false_term);
  flat_atoms.reserve(theory_atoms.size());
  for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
    flat_atoms.push_back(flatten_assigned_atom(
        flat, terms, AssignedTheoryAtom{atom.lit.value, false, atom.term}, false_flat));
  }

  base_closure.ensure_node_count(flat.size());
  for (const theory::equality::FlatDefinition& definition : flat.definitions()) {
    base_closure.add_definition(definition);
  }
  base_closure.assert_distinct(true_flat, false_flat);
}

void assert_flat_assigned_atom(theory::equality::FlatCongruenceClosure& closure,
                               const FlatAssignedTheoryAtom& atom,
                               theory::equality::FlatTermId true_flat,
                               theory::equality::FlatTermId false_flat) {
  if (atom.equality) {
    if (atom.source.value) {
      closure.assert_equal(atom.lhs, atom.rhs, {blocking_lit(atom.source)});
    } else {
      closure.assert_distinct(atom.lhs, atom.rhs, {blocking_lit(atom.source)});
    }
    return;
  }
  closure.assert_equal(atom.lhs, atom.source.value ? true_flat : false_flat,
                       {blocking_lit(atom.source)});
}

std::optional<std::vector<sat::Lit>> flat_candidate_explanation(
    theory::equality::FlatCongruenceClosure& closure, const FlatAssignedTheoryAtom& atom,
    theory::equality::FlatTermId true_flat, theory::equality::FlatTermId false_flat) {
  if (atom.equality) {
    if (!atom.source.value) {
      return std::nullopt;
    }
    return closure.explain_equal(atom.lhs, atom.rhs);
  }
  return closure.explain_equal(atom.lhs, atom.source.value ? true_flat : false_flat);
}

std::optional<sat::Clause> flat_model_propagation_clause_for(
    const core::TermManager& terms, const std::vector<AssignedTheoryAtom>& atoms,
    std::size_t candidate_index, core::TermId true_term, core::TermId false_term,
    util::Statistics* statistics) {
  theory::equality::FlatTermIndex flat(terms);
  const theory::equality::FlatTermId true_flat = flat.flatten(true_term);
  const theory::equality::FlatTermId false_flat = flat.flatten(false_term);
  std::vector<FlatAssignedTheoryAtom> flat_atoms;
  flat_atoms.reserve(atoms.size());
  for (const AssignedTheoryAtom& atom : atoms) {
    flat_atoms.push_back(flatten_assigned_atom(flat, terms, atom, false_flat));
  }

  theory::equality::FlatCongruenceClosure closure(flat.size());
  for (const theory::equality::FlatDefinition& definition : flat.definitions()) {
    closure.add_definition(definition);
  }
  closure.assert_distinct(true_flat, false_flat);
  for (std::size_t i = 0; i < flat_atoms.size(); ++i) {
    if (i == candidate_index) {
      continue;
    }
    const FlatAssignedTheoryAtom& atom = flat_atoms[i];
    if (atom.equality && !atom.source.value) {
      continue;
    }
    assert_flat_assigned_atom(closure, atom, true_flat, false_flat);
  }

  std::optional<std::vector<sat::Lit>> reason =
      flat_candidate_explanation(closure, flat_atoms[candidate_index], true_flat, false_flat);
  if (statistics != nullptr) {
    add_incremental_stats(*statistics, closure.stats());
  }
  if (!reason.has_value()) {
    return std::nullopt;
  }
  reason->push_back(assigned_lit(flat_atoms[candidate_index].source));
  return reason;
}

class IncrementalFlatAssignmentState {
 public:
  IncrementalFlatAssignmentState(const core::TermManager& terms,
                                 const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                                 core::TermId true_term, core::TermId false_term,
                                 util::Statistics& statistics)
      : terms_(terms), flat_(terms), closure_(0), statistics_(statistics) {
    true_flat_ = flat_.flatten(true_term);
    false_flat_ = flat_.flatten(false_term);
    flat_atoms_.reserve(theory_atoms.size());
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      flat_atom_by_var_.emplace(var, flat_atoms_.size());
      flat_atoms_.push_back(flatten_assigned_atom(
          flat_, terms_, AssignedTheoryAtom{atom.lit.value, false, atom.term}, false_flat_));
    }

    closure_.ensure_node_count(flat_.size());
    for (const theory::equality::FlatDefinition& definition : flat_.definitions()) {
      closure_.add_definition(definition);
    }
    closure_.assert_distinct(true_flat_, false_flat_);
    publish_stats();
  }

  void push_level() { checkpoints_.push_back(closure_.push()); }

  void pop_to_level(std::size_t new_level) {
    while (checkpoints_.size() > new_level) {
      const std::size_t checkpoint = checkpoints_.back();
      checkpoints_.pop_back();
      closure_.pop(checkpoint);
      publish_stats();
    }
  }

  void assert_assignment(const AssignedTheoryAtom& assignment) {
    std::optional<FlatAssignedTheoryAtom> flat_atom = flat_atom_for_lit(assignment.lit);
    if (!flat_atom.has_value()) {
      return;
    }
    flat_atom->source.value = assignment.value;
    assert_flat_assigned_atom(closure_, flat_atom.value(), true_flat_, false_flat_);
    publish_stats();
  }

  std::optional<sat::Clause> explain_candidate(const BooleanClausifier::TheoryAtom& atom,
                                               bool value) {
    std::optional<FlatAssignedTheoryAtom> flat_atom = flat_atom_for_lit(atom.lit.value);
    if (!flat_atom.has_value()) {
      return std::nullopt;
    }
    flat_atom->source.value = value;
    if (flat_atom->equality && !value) {
      return std::nullopt;
    }
    std::optional<std::vector<sat::Lit>> reason =
        flat_candidate_explanation(closure_, flat_atom.value(), true_flat_, false_flat_);
    if (!reason.has_value()) {
      return std::nullopt;
    }
    statistics_.increment(kStatEufIncrementalExplainLength, reason->size());
    reason->push_back(assigned_lit(flat_atom->source));
    return reason;
  }

  std::optional<sat::Clause> conflict() const { return closure_.conflict(); }

 private:
  std::optional<FlatAssignedTheoryAtom> flat_atom_for_lit(int lit) const {
    const int var = lit > 0 ? lit : -lit;
    const auto found = flat_atom_by_var_.find(var);
    if (found == flat_atom_by_var_.end()) {
      return std::nullopt;
    }
    return flat_atoms_[found->second];
  }

  void publish_stats() {
    const theory::equality::IncrementalCongruenceClosureStats current = closure_.stats();
    add_incremental_stats(statistics_, stats_delta(current, last_published_stats_));
    last_published_stats_ = current;
  }

  const core::TermManager& terms_;
  theory::equality::FlatTermIndex flat_;
  theory::equality::FlatCongruenceClosure closure_;
  util::Statistics& statistics_;
  theory::equality::FlatTermId true_flat_ = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId false_flat_ = theory::equality::kInvalidFlatTermId;
  std::vector<FlatAssignedTheoryAtom> flat_atoms_;
  std::unordered_map<int, std::size_t> flat_atom_by_var_;
  std::vector<std::size_t> checkpoints_;
  theory::equality::IncrementalCongruenceClosureStats last_published_stats_;
};

struct ModelPropagationResult {
  std::vector<sat::Clause> clauses;
  std::size_t candidates = 0;
  std::size_t reason_lits = 0;
  std::size_t candidate_budget = 0;
  std::size_t delivery_budget = 0;
  bool budget_cutoff = false;
  bool hit_rate_cutoff = false;
};

#if PRIMO_HAVE_IPASIR
class OnlineEufPropagator final : public CaDiCaL::ExternalPropagator {
 public:
  OnlineEufPropagator(const core::TermManager& terms,
                      const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                      core::TermId true_term, core::TermId false_term,
                      const CheckSatOptions& options, util::Statistics& statistics)
      : terms_(terms),
        theory_atoms_(theory_atoms),
        true_term_(true_term),
        false_term_(false_term),
        options_(options),
        statistics_(statistics) {
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      observed_vars_.insert(var);
    }
    if (options_.euf_engine == EufEngine::Incremental) {
      incremental_state_.emplace(terms_, theory_atoms_, true_term_, false_term_, statistics_);
    }
  }

  void notify_assignment(const std::vector<int>& lits) override {
    for (const int lit : lits) {
      const int var = lit > 0 ? lit : -lit;
      if (!observed_vars_.contains(var)) {
        continue;
      }
      if (assignment_by_var_.contains(var)) {
        continue;
      }
      assignment_by_var_[var] = lit > 0;
      trail_.push_back(lit);
      pending_vars_.erase(var);
      if (incremental_state_.has_value()) {
        const BooleanClausifier::TheoryAtom* atom = atom_for_var(var);
        if (atom != nullptr) {
          const bool atom_value = atom->lit.value > 0 ? lit > 0 : lit < 0;
          incremental_state_->assert_assignment(
              AssignedTheoryAtom{atom->lit.value, atom_value, atom->term});
        }
      }
    }
    pending_.clear();
  }

  void notify_new_decision_level() override {
    level_starts_.push_back(trail_.size());
    if (incremental_state_.has_value()) {
      incremental_state_->push_level();
    }
  }

  void notify_backtrack(size_t new_level) override {
    const std::size_t keep = new_level < level_starts_.size() ? level_starts_[new_level] : 0;
    while (trail_.size() > keep) {
      const int lit = trail_.back();
      trail_.pop_back();
      const int var = lit > 0 ? lit : -lit;
      assignment_by_var_.erase(var);
      pending_vars_.erase(var);
    }
    level_starts_.resize(std::min<std::size_t>(new_level + 1, level_starts_.size()));
    if (level_starts_.empty()) {
      level_starts_.push_back(0);
    }
    if (incremental_state_.has_value()) {
      incremental_state_->pop_to_level(new_level);
    }
    pending_.clear();
    pending_vars_.clear();
    external_clause_.clear();
    external_clause_index_ = 0;
  }

  bool cb_check_found_model(const std::vector<int>& /*model*/) override { return true; }

  int cb_propagate() override {
    if (options_.euf_propagation == EufPropagationMode::Conflict) {
      return 0;
    }
    if (pending_.empty()) {
      compute_pending_propagations();
    }
    if (pending_.empty()) {
      return 0;
    }
    const int lit = pending_.front();
    pending_.pop_front();
    pending_vars_.erase(lit > 0 ? lit : -lit);
    return lit;
  }

  int cb_add_reason_clause_lit(int propagated_lit) override {
    if (!reasons_.contains(propagated_lit)) {
      compute_reason_for(propagated_lit);
    }
    const auto found = reasons_.find(propagated_lit);
    if (found == reasons_.end()) {
      return 0;
    }
    Reason& reason = found->second;
    if (reason.active_lit != propagated_lit) {
      reason.active_lit = propagated_lit;
      reason.index = 0;
    }
    if (reason.index == 0) {
      ++reason.index;
      return propagated_lit;
    }
    const std::size_t antecedent_index = reason.index - 1;
    if (antecedent_index >= reason.antecedents.size()) {
      reason.index = 0;
      reason.active_lit = 0;
      return 0;
    }
    ++reason.index;
    return reason.antecedents[antecedent_index];
  }

  bool cb_has_external_clause(bool& is_forgettable) override {
    maybe_store_conflict();
    if (external_clause_.empty()) {
      return false;
    }
    is_forgettable = false;
    return true;
  }

  int cb_add_external_clause_lit() override {
    if (external_clause_index_ >= external_clause_.size()) {
      external_clause_.clear();
      external_clause_index_ = 0;
      return 0;
    }
    return external_clause_[external_clause_index_++].value;
  }

 private:
  struct Reason {
    std::vector<int> antecedents;
    std::size_t index = 0;
    int active_lit = 0;
  };

  const BooleanClausifier::TheoryAtom* atom_for_var(int var) const {
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      const int atom_var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (atom_var == var) {
        return &atom;
      }
    }
    return nullptr;
  }

  std::vector<AssignedTheoryAtom> current_assigned_atoms() const {
    std::vector<AssignedTheoryAtom> atoms;
    atoms.reserve(assignment_by_var_.size());
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      const auto assignment = assignment_by_var_.find(var);
      if (assignment == assignment_by_var_.end()) {
        continue;
      }
      const bool atom_value = atom.lit.value > 0 ? assignment->second : !assignment->second;
      atoms.push_back(AssignedTheoryAtom{atom.lit.value, atom_value, atom.term});
    }
    return atoms;
  }

  bool reason_is_valid_for_current_assignment(int propagated_lit, const sat::Clause& clause) const {
    if (clause.empty()) {
      return false;
    }
    std::size_t propagated_count = 0;
    for (const sat::Lit lit : clause) {
      if (lit.value == propagated_lit) {
        ++propagated_count;
        continue;
      }
      const int var = lit.value > 0 ? lit.value : -lit.value;
      if (!observed_vars_.contains(var)) {
        return false;
      }
      const auto assignment = assignment_by_var_.find(var);
      if (assignment == assignment_by_var_.end()) {
        return false;
      }
      const bool literal_is_true = lit.value > 0 ? assignment->second : !assignment->second;
      if (literal_is_true) {
        return false;
      }
    }
    return propagated_count == 1;
  }

  bool store_reason(int propagated_lit, const sat::Clause& clause) {
    if (!reason_is_valid_for_current_assignment(propagated_lit, clause)) {
      return false;
    }
    std::vector<int> antecedents;
    antecedents.reserve(clause.size());
    for (const sat::Lit lit : clause) {
      if (lit.value != propagated_lit) {
        antecedents.push_back(lit.value);
      }
    }
    reasons_[propagated_lit] = Reason{std::move(antecedents), 0, 0};
    return true;
  }

  void compute_reason_for(int propagated_lit) {
    const int var = propagated_lit > 0 ? propagated_lit : -propagated_lit;
    const BooleanClausifier::TheoryAtom* atom = atom_for_var(var);
    if (atom == nullptr) {
      return;
    }
    const bool value = propagated_lit == atom->lit.value;
    const core::Term& term = terms_.get(atom->term);
    if (term.kind == core::TermKind::Equality && !value) {
      return;
    }

    std::vector<AssignedTheoryAtom> atoms;
    for (const AssignedTheoryAtom& assigned : current_assigned_atoms()) {
      const int assigned_var = assigned.lit > 0 ? assigned.lit : -assigned.lit;
      if (assigned_var != var) {
        atoms.push_back(assigned);
      }
    }
    atoms.push_back(AssignedTheoryAtom{atom->lit.value, value, atom->term});
    std::optional<sat::Clause> clause =
        options_.euf_engine == EufEngine::Incremental
            ? flat_model_propagation_clause_for(terms_, atoms, atoms.size() - 1, true_term_,
                                                false_term_, &statistics_)
            : model_propagation_clause_for(terms_, atoms, atoms.size() - 1, true_term_,
                                           false_term_);
    if (clause.has_value()) {
      (void)store_reason(propagated_lit, clause.value());
    }
  }

  void compute_pending_propagations() {
    statistics_.increment(kStatEufPropagationRounds);
    if (options_.euf_propagation_interval > 1 &&
        (statistics_.get(kStatEufPropagationRounds) - 1) % options_.euf_propagation_interval != 0) {
      return;
    }
    const std::vector<AssignedTheoryAtom> assigned = current_assigned_atoms();
    const std::size_t theory_atoms = theory_atoms_.size();
    if (theory_atoms > 0 &&
        static_cast<double>(assigned.size()) / static_cast<double>(theory_atoms) >
            options_.euf_propagation_assign_threshold) {
      statistics_.increment(kStatEufPropagationHitRateCutoffs);
      return;
    }
    const std::optional<std::size_t> candidate_limit =
        resolve_budget(options_.euf_propagation_budget, theory_atoms);
    const std::size_t candidate_budget = candidate_limit.value_or(theory_atoms);
    const std::optional<std::size_t> delivery_limit =
        resolve_delivery_budget(options_.euf_propagation_delivery, candidate_budget, theory_atoms);
    statistics_.increment(kStatEufPropagationBudget, candidate_limit.value_or(0));
    statistics_.increment(kStatEufPropagationDeliveryBudget, delivery_limit.value_or(0));

    std::size_t inspected = 0;
    std::size_t delivered = 0;
    constexpr std::size_t kMinInspectionsBeforeHitRateCutoff = 16;
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      if (candidate_limit.has_value() && inspected >= candidate_limit.value()) {
        statistics_.increment(kStatEufPropagationBudgetCutoffs);
        break;
      }
      if (delivery_limit.has_value() && delivered >= delivery_limit.value()) {
        statistics_.increment(kStatEufPropagationBudgetCutoffs);
        break;
      }
      if (inspected >= kMinInspectionsBeforeHitRateCutoff && delivered == 0 &&
          options_.euf_propagation_budget.kind == AutoBudget::Kind::Auto) {
        statistics_.increment(kStatEufPropagationHitRateCutoffs);
        break;
      }

      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (assignment_by_var_.contains(var) || pending_vars_.contains(var)) {
        continue;
      }
      ++inspected;
      if (try_online_candidate(assigned, atom, true) ||
          try_online_candidate(assigned, atom, false)) {
        ++delivered;
      }
    }
    statistics_.increment(kStatEufPropagationCandidates, inspected);
  }

  bool try_online_candidate(const std::vector<AssignedTheoryAtom>& assigned,
                            const BooleanClausifier::TheoryAtom& atom, bool value) {
    const core::Term& term = terms_.get(atom.term);
    if (term.kind == core::TermKind::Equality && !value) {
      return false;
    }
    std::optional<sat::Clause> clause;
    if (incremental_state_.has_value()) {
      clause = incremental_state_->explain_candidate(atom, value);
    } else {
      std::vector<AssignedTheoryAtom> atoms = assigned;
      atoms.push_back(AssignedTheoryAtom{atom.lit.value, value, atom.term});
      clause =
          model_propagation_clause_for(terms_, atoms, atoms.size() - 1, true_term_, false_term_);
    }
    if (!clause.has_value()) {
      return false;
    }
    const int propagated_lit = value ? atom.lit.value : -atom.lit.value;
    const int var = propagated_lit > 0 ? propagated_lit : -propagated_lit;
    if (!store_reason(propagated_lit, clause.value())) {
      return false;
    }
    pending_.push_back(propagated_lit);
    pending_vars_.insert(var);
    statistics_.increment(kStatEufPropagationDelivered);
    statistics_.increment(kStatEufPropagationReasonLits,
                          reasons_[propagated_lit].antecedents.size());
    return true;
  }

  void maybe_store_conflict() {
    if (!external_clause_.empty() || !incremental_state_.has_value()) {
      return;
    }
    std::optional<sat::Clause> conflict = incremental_state_->conflict();
    if (!conflict.has_value()) {
      return;
    }
    external_clause_ = std::move(conflict.value());
    external_clause_index_ = 0;
    statistics_.increment(kStatTheoryConflicts);
  }

  const core::TermManager& terms_;
  const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms_;
  core::TermId true_term_ = core::kInvalidTermId;
  core::TermId false_term_ = core::kInvalidTermId;
  const CheckSatOptions& options_;
  util::Statistics& statistics_;
  std::unordered_set<int> observed_vars_;
  std::unordered_map<int, bool> assignment_by_var_;
  std::vector<int> trail_;
  std::vector<std::size_t> level_starts_{0};
  std::deque<int> pending_;
  std::unordered_set<int> pending_vars_;
  std::unordered_map<int, Reason> reasons_;
  std::optional<IncrementalFlatAssignmentState> incremental_state_;
  sat::Clause external_clause_;
  std::size_t external_clause_index_ = 0;
};
#endif

ModelPropagationResult learn_model_propagations(const core::TermManager& terms,
                                                const std::vector<AssignedTheoryAtom>& atoms,
                                                core::TermId true_term, core::TermId false_term,
                                                const CheckSatOptions& options,
                                                std::unordered_set<std::string>& learned_clauses,
                                                util::Statistics& statistics) {
  ModelPropagationResult result;
  const std::size_t theory_atoms = atoms.size();
  const std::optional<std::size_t> candidate_limit =
      resolve_budget(options.euf_propagation_budget, theory_atoms);
  const std::size_t candidate_budget = candidate_limit.value_or(theory_atoms);
  const std::optional<std::size_t> delivery_limit =
      resolve_delivery_budget(options.euf_propagation_delivery, candidate_budget, theory_atoms);
  result.candidate_budget = candidate_limit.value_or(0);
  result.delivery_budget = delivery_limit.value_or(0);

  constexpr std::size_t kMinInspectionsBeforeHitRateCutoff = 16;
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (candidate_limit.has_value() && result.candidates >= candidate_limit.value()) {
      result.budget_cutoff = true;
      break;
    }
    if (delivery_limit.has_value() && result.clauses.size() >= delivery_limit.value()) {
      result.budget_cutoff = true;
      break;
    }
    if (result.candidates >= kMinInspectionsBeforeHitRateCutoff && result.clauses.empty() &&
        options.euf_propagation_budget.kind == AutoBudget::Kind::Auto) {
      result.hit_rate_cutoff = true;
      break;
    }

    ++result.candidates;
    std::optional<sat::Clause> clause =
        options.euf_engine == EufEngine::Incremental
            ? flat_model_propagation_clause_for(terms, atoms, i, true_term, false_term, &statistics)
            : model_propagation_clause_for(terms, atoms, i, true_term, false_term);
    if (!clause.has_value()) {
      continue;
    }
    const std::string key = clause_key(clause.value());
    if (!learned_clauses.insert(key).second) {
      continue;
    }
    result.reason_lits += clause->empty() ? 0 : clause->size() - 1;
    result.clauses.push_back(std::move(clause.value()));
  }
  return result;
}

std::optional<sat::Clause> find_theory_conflict(const core::TermManager& terms,
                                                const std::vector<AssignedTheoryAtom>& atoms,
                                                core::TermId true_term, core::TermId false_term) {
  theory::equality::ExplainedCongruenceClosure closure(terms);
  std::vector<AssignedTheoryAtom> disequalities;

  closure.add_term(true_term);
  closure.add_term(false_term);
  for (const AssignedTheoryAtom& atom : atoms) {
    const core::Term& term = terms.get(atom.term);
    if (term.kind == core::TermKind::Equality && !atom.value) {
      disequalities.push_back(atom);
      continue;
    }
    if (!add_explained_theory_equality(closure, terms, atom, true_term, false_term)) {
      throw UnsupportedInput("unsupported theory atom");
    }
  }

  if (std::optional<std::vector<sat::Lit>> reason = closure.explain_equal(true_term, false_term);
      reason.has_value()) {
    return reason.value();
  }

  for (const AssignedTheoryAtom& atom : disequalities) {
    const core::Term& term = terms.get(atom.term);
    if (std::optional<std::vector<sat::Lit>> reason =
            closure.explain_equal(term.children.at(0), term.children.at(1));
        reason.has_value()) {
      sat::Clause conflict = reason.value();
      conflict.push_back(blocking_lit(atom));
      return conflict;
    }
  }

  return std::nullopt;
}

std::optional<sat::Clause> find_incremental_theory_conflict(
    const PreparedFlatTheory& prepared, const std::vector<AssignedTheoryAtom>& atoms,
    util::Statistics& statistics) {
  if (atoms.size() != prepared.flat_atoms.size()) {
    throw UnsupportedInput("internal theory atom mapping mismatch");
  }

  theory::equality::FlatCongruenceClosure closure = prepared.base_closure;
  for (std::size_t i = 0; i < prepared.flat_atoms.size(); ++i) {
    FlatAssignedTheoryAtom atom = prepared.flat_atoms[i];
    atom.source.value = atoms[i].value;
    assert_flat_assigned_atom(closure, atom, prepared.true_flat, prepared.false_flat);
    if (closure.conflict().has_value()) {
      break;
    }
  }

  add_incremental_stats(statistics, closure.stats());
  if (closure.conflict().has_value()) {
    return closure.conflict().value();
  }
  return std::nullopt;
}

CheckSatOutcome solve_qf_uf(core::TermManager& terms, const std::vector<core::TermId>& assertions,
                            const CheckSatOptions& options, util::Statistics& statistics) {
  statistics.increment(kStatSolverQfUf);
  sat::CadicalBackend backend;
  BooleanClausifier clausifier(backend, terms);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    backend.phase(sat::Lit{-atom.lit.value});
    statistics.increment(kStatSatTheoryNegativePhase);
  }

  const core::TermId true_term = terms.mk_bool(true);
  const core::TermId false_term = terms.mk_bool(false);
  std::unordered_set<std::string> learned_model_propagations;
  std::optional<PreparedFlatTheory> prepared_flat_theory;
  if (options.euf_engine == EufEngine::Incremental) {
    prepared_flat_theory.emplace(terms, clausifier.theory_atoms(), true_term, false_term);
  }

#if PRIMO_HAVE_IPASIR
  std::optional<OnlineEufPropagator> online_propagator;
  if (options.euf_propagation == EufPropagationMode::Online ||
      options.euf_propagation == EufPropagationMode::Conflict) {
    online_propagator.emplace(terms, clausifier.theory_atoms(), true_term, false_term, options,
                              statistics);
    backend.connect_external_propagator(&online_propagator.value());
    for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      backend.add_observed_var(var);
    }
  }
#else
  if (options.euf_propagation == EufPropagationMode::Online ||
      options.euf_propagation == EufPropagationMode::Conflict) {
    return CheckSatOutcome{CheckSatResult::Unsupported,
                           "online EUF conflict/propagation requires CaDiCaL external propagators",
                           statistics};
  }
#endif

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
      const bool atom_value = atom.lit.value > 0 ? assignment.value() : !assignment.value();
      assigned_atoms.push_back(AssignedTheoryAtom{atom.lit.value, atom_value, atom.term});
    }

    if (options.euf_propagation == EufPropagationMode::Model) {
      statistics.increment(kStatEufPropagationRounds);
      ModelPropagationResult propagation =
          learn_model_propagations(terms, assigned_atoms, true_term, false_term, options,
                                   learned_model_propagations, statistics);
      statistics.increment(kStatEufPropagationCandidates, propagation.candidates);
      statistics.increment(kStatEufPropagationDelivered, propagation.clauses.size());
      statistics.increment(kStatEufPropagationReasonLits, propagation.reason_lits);
      statistics.increment(kStatEufPropagationBudget, propagation.candidate_budget);
      statistics.increment(kStatEufPropagationDeliveryBudget, propagation.delivery_budget);
      if (propagation.budget_cutoff) {
        statistics.increment(kStatEufPropagationBudgetCutoffs);
      }
      if (propagation.hit_rate_cutoff) {
        statistics.increment(kStatEufPropagationHitRateCutoffs);
      }
      for (const sat::Clause& clause : propagation.clauses) {
        backend.add_clause(clause);
      }
    }

    std::optional<sat::Clause> conflict_clause =
        options.euf_engine == EufEngine::Incremental
            ? find_incremental_theory_conflict(prepared_flat_theory.value(), assigned_atoms,
                                               statistics)
            : find_theory_conflict(terms, assigned_atoms, true_term, false_term);
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
  statistics.set(kStatFiniteDomainDomains, 0);
  statistics.set(kStatFiniteDomainAmoClauses, 0);
  statistics.set(kStatFiniteDomainEvaluatedEqualities, 0);
  statistics.set(kStatFiniteDomainEvaluationCutoffs, 0);
  statistics.set(kStatEqualityDefinitions, 0);
  statistics.set(kStatGroundCongruenceClauses, 0);
  statistics.set(kStatGroundCongruenceSkippedGroups, 0);
  statistics.set(kStatUfSymmetryAcceptedSets, 0);
  statistics.set(kStatUfSymmetryConstraints, 0);
  statistics.set(kStatUfSymmetryRejectedNoninvariant, 0);
  statistics.set(kStatAutoSingleAssertion, 0);
  statistics.set(kStatAutoStrong, 0);
  statistics.set(kStatSatTheoryNegativePhase, 0);
  statistics.set(kStatSolverBoolean, 0);
  statistics.set(kStatSolverQfUf, 0);
  statistics.set(kStatSatSolveCalls, 0);
  statistics.set(kStatTheoryAtoms, 0);
  statistics.set(kStatTheoryChecks, 0);
  statistics.set(kStatTheoryConflicts, 0);
  statistics.set(kStatEufPropagationRounds, 0);
  statistics.set(kStatEufPropagationCandidates, 0);
  statistics.set(kStatEufPropagationDelivered, 0);
  statistics.set(kStatEufPropagationReasonLits, 0);
  statistics.set(kStatEufPropagationBudget, 0);
  statistics.set(kStatEufPropagationDeliveryBudget, 0);
  statistics.set(kStatEufPropagationBudgetCutoffs, 0);
  statistics.set(kStatEufPropagationHitRateCutoffs, 0);
  statistics.set(kStatEufIncrementalTerms, 0);
  statistics.set(kStatEufIncrementalFlatDefinitions, 0);
  statistics.set(kStatEufIncrementalMerges, 0);
  statistics.set(kStatEufIncrementalPendingPops, 0);
  statistics.set(kStatEufIncrementalLookupProbes, 0);
  statistics.set(kStatEufIncrementalLookupHits, 0);
  statistics.set(kStatEufIncrementalCongruenceMerges, 0);
  statistics.set(kStatEufIncrementalDisequalityChecks, 0);
  statistics.set(kStatEufIncrementalConflicts, 0);
  statistics.set(kStatEufIncrementalExplainLength, 0);
  statistics.set(kStatEufIncrementalBacktracks, 0);

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

    const bool auto_single_assertion =
        options.qf_uf_preprocess == QfUfPreprocessMode::Auto && assertions.size() <= 1;
    const bool auto_strong =
        options.qf_uf_preprocess == QfUfPreprocessMode::Auto && assertions.size() > 1;
    if (auto_single_assertion) {
      statistics.increment(kStatAutoSingleAssertion);
    }
    if (auto_strong) {
      statistics.increment(kStatAutoStrong);
    }
    const bool strong_preprocessing =
        options.qf_uf_preprocess == QfUfPreprocessMode::Strong || auto_strong;
    const bool preprocessing_enabled = options.qf_uf_preprocess != QfUfPreprocessMode::Off;
    const bool use_eq_bridge = options.eq_bridge || strong_preprocessing || auto_single_assertion;
    const bool use_finite_domain_evaluation = false;
    const bool use_finite_domain_amo = strong_preprocessing;
    const bool use_finite_domain_equality_definitions = strong_preprocessing;
    const bool use_ground_congruence_definitions = options.ground_congruence;
    const bool use_symmetry_breaking =
        preprocessing_enabled && (options.uf_symmetry_breaking || strong_preprocessing);
    const std::size_t preprocessing_passes =
        (strong_preprocessing || auto_single_assertion)
            ? std::max<std::size_t>(options.preprocessing_passes, 3)
            : options.preprocessing_passes;

    if (use_symmetry_breaking) {
      UfSymmetryBreaker symmetry_breaker(frontend.terms());
      UfSymmetryBreakingResult symmetry = symmetry_breaker.break_symmetries(assertions);
      assertions.insert(assertions.end(), symmetry.constraints.begin(), symmetry.constraints.end());
      statistics.increment(kStatUfSymmetryAcceptedSets, symmetry.accepted_sets);
      statistics.increment(kStatUfSymmetryConstraints, symmetry.constraints.size());
      statistics.increment(kStatUfSymmetryRejectedNoninvariant, symmetry.rejected_noninvariant);
    }
    for (std::size_t pass = 0; preprocessing_enabled && pass < preprocessing_passes; ++pass) {
      const std::vector<core::TermId> before = assertions;
      statistics.increment(kStatPreprocessingPasses);
      if (use_eq_bridge) {
        EqualityBridge bridge(frontend.terms());
        EqualityBridgeResult bridge_result = bridge.bridge(assertions);
        assertions.insert(assertions.end(), bridge_result.equalities.begin(),
                          bridge_result.equalities.end());
        statistics.increment(kStatEqBridgeEqualities, bridge_result.equalities.size());
        statistics.increment(kStatEqBridgeSkippedOrNodes, bridge_result.skipped_or_nodes);
      }
      if (use_finite_domain_evaluation) {
        FiniteDomainEvaluator evaluator(frontend.terms());
        FiniteDomainEvaluationResult evaluation_result = evaluator.simplify(assertions);
        assertions = std::move(evaluation_result.assertions);
        statistics.increment(kStatFiniteDomainEvaluatedEqualities,
                             evaluation_result.equalities_simplified);
        statistics.increment(kStatFiniteDomainEvaluationCutoffs,
                             evaluation_result.enumeration_cutoffs);
      }
      if (use_finite_domain_amo) {
        FiniteDomainAtMostOne finite_domain(frontend.terms());
        FiniteDomainAtMostOneResult finite_domain_result = finite_domain.generate(assertions);
        assertions.insert(assertions.end(), finite_domain_result.clauses.begin(),
                          finite_domain_result.clauses.end());
        statistics.increment(kStatFiniteDomainDomains, finite_domain_result.domains);
        statistics.increment(kStatFiniteDomainAmoClauses, finite_domain_result.clauses.size());
      }
      if (use_finite_domain_equality_definitions) {
        FiniteDomainEqualityDefinitions equality_definitions(frontend.terms());
        FiniteDomainEqualityDefinitionsResult equality_definition_result =
            equality_definitions.generate(assertions);
        assertions.insert(assertions.end(), equality_definition_result.clauses.begin(),
                          equality_definition_result.clauses.end());
        statistics.increment(kStatEqualityDefinitions, equality_definition_result.clauses.size());
      }
      if (use_ground_congruence_definitions) {
        CongruenceDefinitionGenerator congruence_definitions(frontend.terms());
        CongruenceDefinitionResult congruence_definition_result =
            congruence_definitions.generate(assertions);
        assertions.insert(assertions.end(), congruence_definition_result.clauses.begin(),
                          congruence_definition_result.clauses.end());
        statistics.increment(kStatGroundCongruenceClauses,
                             congruence_definition_result.clauses.size());
        statistics.increment(kStatGroundCongruenceSkippedGroups,
                             congruence_definition_result.skipped_groups);
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
      return solve_qf_uf(frontend.terms(), assertions, options, statistics);
    }
    try {
      return solve_boolean_conjunction(frontend.terms(), assertions, statistics);
    } catch (const UnsupportedInput&) {
      return solve_qf_uf(frontend.terms(), assertions, options, statistics);
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
