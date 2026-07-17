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

constexpr util::StatKey kStatAssertionsInitial = util::StatKey::AssertionsInitial;
constexpr util::StatKey kStatFrontendBooleanItes = util::StatKey::FrontendBooleanItes;
constexpr util::StatKey kStatFrontendAuxiliaryItes = util::StatKey::FrontendAuxiliaryItes;
constexpr util::StatKey kStatAssertionsFinal = util::StatKey::AssertionsFinal;
constexpr util::StatKey kStatPreprocessingPasses = util::StatKey::PreprocessingPasses;
constexpr util::StatKey kStatPreprocessingChangedPasses = util::StatKey::PreprocessingChangedPasses;
constexpr util::StatKey kStatPreprocessingAdaptivePasses =
    util::StatKey::PreprocessingAdaptivePasses;
constexpr util::StatKey kStatPreprocessingTermSizeInitial =
    util::StatKey::PreprocessingTermSizeInitial;
constexpr util::StatKey kStatPreprocessingTermSizeFinal = util::StatKey::PreprocessingTermSizeFinal;
constexpr util::StatKey kStatEqBridgeEqualities = util::StatKey::EqBridgeEqualities;
constexpr util::StatKey kStatEqBridgeSkippedOrNodes = util::StatKey::EqBridgeSkippedOrNodes;
constexpr util::StatKey kStatBooleanUnitFacts = util::StatKey::BooleanUnitFacts;
constexpr util::StatKey kStatBooleanUnitRewrites = util::StatKey::BooleanUnitRewrites;
constexpr util::StatKey kStatForcedAtomFacts = util::StatKey::ForcedAtomFacts;
constexpr util::StatKey kStatForcedAtomDisequalities = util::StatKey::ForcedAtomDisequalities;
constexpr util::StatKey kStatForcedAtomRewrites = util::StatKey::ForcedAtomRewrites;
constexpr util::StatKey kStatUnitEqualityConsequences = util::StatKey::UnitEqualityConsequences;
constexpr util::StatKey kStatUnitEqualitySubstitutionUnits =
    util::StatKey::UnitEqualitySubstitutionUnits;
constexpr util::StatKey kStatUnitEqualitySubstitutions = util::StatKey::UnitEqualitySubstitutions;
constexpr util::StatKey kStatUnitEqualitySubstitutionAccepted =
    util::StatKey::UnitEqualitySubstitutionAccepted;
constexpr util::StatKey kStatUnitEqualitySubstitutionRejected =
    util::StatKey::UnitEqualitySubstitutionRejected;
constexpr util::StatKey kStatSolvedConstantCandidates = util::StatKey::SolvedConstantCandidates;
constexpr util::StatKey kStatSolvedConstantAccepted = util::StatKey::SolvedConstantAccepted;
constexpr util::StatKey kStatSolvedConstantRewrites = util::StatKey::SolvedConstantRewrites;
constexpr util::StatKey kStatSolvedConstantRejectedCycles =
    util::StatKey::SolvedConstantRejectedCycles;
constexpr util::StatKey kStatSolvedConstantRejectedBlowups =
    util::StatKey::SolvedConstantRejectedBlowups;
constexpr util::StatKey kStatFiniteDomainDomains = util::StatKey::FiniteDomainDomains;
constexpr util::StatKey kStatFiniteDomainAmoClauses = util::StatKey::FiniteDomainAmoClauses;
constexpr util::StatKey kStatFiniteDomainEvaluatedEqualities =
    util::StatKey::FiniteDomainEvaluatedEqualities;
constexpr util::StatKey kStatFiniteDomainEvaluationCutoffs =
    util::StatKey::FiniteDomainEvaluationCutoffs;
constexpr util::StatKey kStatEqualityDefinitions = util::StatKey::EqualityDefinitions;
constexpr util::StatKey kStatGroundCongruenceClauses = util::StatKey::GroundCongruenceClauses;
constexpr util::StatKey kStatGroundCongruenceSkippedGroups =
    util::StatKey::GroundCongruenceSkippedGroups;
constexpr util::StatKey kStatUfSymmetryAcceptedSets = util::StatKey::UfSymmetryAcceptedSets;
constexpr util::StatKey kStatUfSymmetryValues = util::StatKey::UfSymmetryValues;
constexpr util::StatKey kStatUfSymmetryTerms = util::StatKey::UfSymmetryTerms;
constexpr util::StatKey kStatUfSymmetryConstraints = util::StatKey::UfSymmetryConstraints;
constexpr util::StatKey kStatUfSymmetryRejectedNoninvariant =
    util::StatKey::UfSymmetryRejectedNoninvariant;
constexpr util::StatKey kStatAutoSingleAssertion = util::StatKey::AutoSingleAssertion;
constexpr util::StatKey kStatAutoStrong = util::StatKey::AutoStrong;
constexpr util::StatKey kStatAutoLargeAssertionSkip = util::StatKey::AutoLargeAssertionSkip;
constexpr util::StatKey kStatSatTheoryNegativePhase = util::StatKey::SatTheoryNegativePhase;
constexpr util::StatKey kStatSatVars = util::StatKey::SatVars;
constexpr util::StatKey kStatSatClauses = util::StatKey::SatClauses;
constexpr util::StatKey kStatSatClauseLits = util::StatKey::SatClauseLits;
constexpr util::StatKey kStatSatUnitClauses = util::StatKey::SatUnitClauses;
constexpr util::StatKey kStatSatBinaryClauses = util::StatKey::SatBinaryClauses;
constexpr util::StatKey kStatSolverBoolean = util::StatKey::SolverBoolean;
constexpr util::StatKey kStatSolverQfUf = util::StatKey::SolverQfUf;
constexpr util::StatKey kStatSatSolveCalls = util::StatKey::SatSolveCalls;
constexpr util::StatKey kStatTheoryAtoms = util::StatKey::TheoryAtoms;
constexpr util::StatKey kStatPureTheoryFilterEnabled = util::StatKey::PureTheoryFilterEnabled;
constexpr util::StatKey kStatPureTheoryAtomsPositive = util::StatKey::PureTheoryAtomsPositive;
constexpr util::StatKey kStatPureTheoryAtomsNegative = util::StatKey::PureTheoryAtomsNegative;
constexpr util::StatKey kStatPureTheoryLiteralsFiltered = util::StatKey::PureTheoryLiteralsFiltered;
constexpr util::StatKey kStatTheoryChecks = util::StatKey::TheoryChecks;
constexpr util::StatKey kStatTheoryConflicts = util::StatKey::TheoryConflicts;
constexpr util::StatKey kStatEufPropagationRounds = util::StatKey::EufPropagationRounds;
constexpr util::StatKey kStatEufPropagationCandidates = util::StatKey::EufPropagationCandidates;
constexpr util::StatKey kStatEufPropagationDelivered = util::StatKey::EufPropagationDelivered;
constexpr util::StatKey kStatEufPropagationReasonLits = util::StatKey::EufPropagationReasonLits;
constexpr util::StatKey kStatEufPropagationBudget = util::StatKey::EufPropagationBudget;
constexpr util::StatKey kStatEufPropagationDeliveryBudget =
    util::StatKey::EufPropagationDeliveryBudget;
constexpr util::StatKey kStatEufPropagationBudgetCutoffs =
    util::StatKey::EufPropagationBudgetCutoffs;
constexpr util::StatKey kStatEufPropagationHitRateCutoffs =
    util::StatKey::EufPropagationHitRateCutoffs;
constexpr util::StatKey kStatEufPropagationAutoOnline = util::StatKey::EufPropagationAutoOnline;
constexpr util::StatKey kStatEufPropagationAutoConflict = util::StatKey::EufPropagationAutoConflict;
constexpr util::StatKey kStatEufPropagationAutoOff = util::StatKey::EufPropagationAutoOff;
constexpr util::StatKey kStatEufIncrementalTerms = util::StatKey::EufIncrementalTerms;
constexpr util::StatKey kStatEufIncrementalFlatDefinitions =
    util::StatKey::EufIncrementalFlatDefinitions;
constexpr util::StatKey kStatEufIncrementalMerges = util::StatKey::EufIncrementalMerges;
constexpr util::StatKey kStatEufIncrementalPendingPops = util::StatKey::EufIncrementalPendingPops;
constexpr util::StatKey kStatEufIncrementalLookupProbes = util::StatKey::EufIncrementalLookupProbes;
constexpr util::StatKey kStatEufIncrementalLookupHits = util::StatKey::EufIncrementalLookupHits;
constexpr util::StatKey kStatEufIncrementalCongruenceMerges =
    util::StatKey::EufIncrementalCongruenceMerges;
constexpr util::StatKey kStatEufIncrementalDisequalityChecks =
    util::StatKey::EufIncrementalDisequalityChecks;
constexpr util::StatKey kStatEufIncrementalConflicts = util::StatKey::EufIncrementalConflicts;
constexpr util::StatKey kStatEufIncrementalExplainCalls = util::StatKey::EufIncrementalExplainCalls;
constexpr util::StatKey kStatEufIncrementalExplainCacheHits =
    util::StatKey::EufIncrementalExplainCacheHits;
constexpr util::StatKey kStatEufIncrementalExplainLength =
    util::StatKey::EufIncrementalExplainLength;
constexpr util::StatKey kStatEufIncrementalBacktracks = util::StatKey::EufIncrementalBacktracks;

constexpr std::size_t kAutoPreprocessLargeAssertionLimit = 10000;

std::size_t reachable_term_count(const core::TermManager& terms,
                                 const std::vector<core::TermId>& roots) {
  std::vector<unsigned char> seen(terms.size(), 0);
  std::size_t count = 0;
  std::vector<core::TermId> work = roots;
  while (!work.empty()) {
    const core::TermId term_id = work.back();
    work.pop_back();
    if (term_id >= seen.size()) {
      seen.resize(term_id + 1, 0);
    }
    if (seen[term_id] != 0) {
      continue;
    }
    seen[term_id] = 1;
    ++count;
    const core::Term& term = terms.get(term_id);
    work.insert(work.end(), term.children.begin(), term.children.end());
  }
  return count;
}

void publish_sat_stats(const sat::BackendStatistics& sat_stats, util::Statistics& statistics) {
  statistics.set(kStatSatVars, sat_stats.variables);
  statistics.set(kStatSatClauses, sat_stats.clauses);
  statistics.set(kStatSatClauseLits, sat_stats.clause_literals);
  statistics.set(kStatSatUnitClauses, sat_stats.unit_clauses);
  statistics.set(kStatSatBinaryClauses, sat_stats.binary_clauses);
}

EufPropagationMode resolve_euf_propagation_mode(const CheckSatOptions& options,
                                                std::size_t theory_atom_count,
                                                util::Statistics& statistics) {
  if (options.euf_propagation != EufPropagationMode::Auto) {
    return options.euf_propagation;
  }
  constexpr std::size_t kAutoOnlineTheoryAtomLimit = 5000;
  if (theory_atom_count <= kAutoOnlineTheoryAtomLimit) {
    statistics.set(kStatEufPropagationAutoOnline, 1);
    return EufPropagationMode::Online;
  }
  statistics.set(kStatEufPropagationAutoConflict, 1);
  return EufPropagationMode::Conflict;
}

bool accept_guarded_ground_substitution(const std::vector<core::TermId>& before,
                                        const std::vector<core::TermId>& after,
                                        const GroundEqualitySubstitutionResult& result,
                                        const core::TermManager& terms) {
  if (result.rewrites == 0 || after == before) {
    return false;
  }
  const std::size_t before_terms = reachable_term_count(terms, before);
  const std::size_t after_terms = reachable_term_count(terms, after);
  const bool shrank_by_five_percent =
      after_terms < before_terms && after_terms * 100 <= before_terms * 95;
  constexpr std::size_t kLargeSubstitutionRewriteFloor = 1500;
  constexpr std::size_t kLargeSubstitutionAssertionBudget = 1000;
  const bool large_bounded_substitution =
      result.rewrites >= kLargeSubstitutionRewriteFloor &&
      after.size() <= before.size() + kLargeSubstitutionAssertionBudget;
  return shrank_by_five_percent || large_bounded_substitution;
}

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
      if (!definitions_.empty()) {
        const auto defined = definitions_.find(expr.atom);
        if (defined != definitions_.end() && defined->second.parameters.empty()) {
          return instantiate_definition(expr.atom, {});
        }
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
      children.reserve(expr.list.size() - 1);
      for (std::size_t i = 1; i < expr.list.size(); ++i) {
        children.push_back(build_term(expr.list[i]));
      }
      return terms_.mk_and(std::move(children));
    }
    if (head == "or") {
      std::vector<core::TermId> children;
      children.reserve(expr.list.size() - 1);
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

    if (!definitions_.empty()) {
      const auto defined = definitions_.find(head);
      if (defined != definitions_.end()) {
        std::vector<core::TermId> args;
        args.reserve(expr.list.size() - 1);
        for (std::size_t i = 1; i < expr.list.size(); ++i) {
          args.push_back(build_term(expr.list[i]));
        }
        return instantiate_definition(head, std::move(args));
      }
    }

    const auto symbol = symbols_.lookup_symbol(head);
    if (!symbol.has_value()) {
      throw UnsupportedInput("unknown function symbol: " + head);
    }
    std::vector<core::TermId> args;
    args.reserve(expr.list.size() - 1);
    for (std::size_t i = 1; i < expr.list.size(); ++i) {
      args.push_back(bind_bool_argument_if_needed(build_term(expr.list[i])));
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

  static bool has_head(const smtlib::SExpr& expr, std::string_view head) {
    return expr.kind == smtlib::SExpr::Kind::List && !expr.list.empty() &&
           expr.list[0].kind == smtlib::SExpr::Kind::Atom && expr.list[0].atom == head;
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
    const smtlib::SExpr* previous_expr = &expr.list[1];
    std::optional<core::TermId> previous_term;
    for (std::size_t i = 2; i < expr.list.size(); ++i) {
      const smtlib::SExpr& current_expr = expr.list[i];
      if (has_head(current_expr, "ite")) {
        const core::TermId lhs =
            previous_term.has_value() ? previous_term.value() : build_term(*previous_expr);
        equalities.push_back(build_pairwise_equality_to_ite(lhs, current_expr));
        previous_expr = &current_expr;
        previous_term.reset();
        continue;
      }
      const core::TermId current = build_term(current_expr);
      if (has_head(*previous_expr, "ite")) {
        equalities.push_back(build_pairwise_equality_to_ite(current, *previous_expr));
      } else {
        const core::TermId previous =
            previous_term.has_value() ? previous_term.value() : build_term(*previous_expr);
        equalities.push_back(build_pairwise_equality(previous, current));
      }
      previous_expr = &current_expr;
      previous_term = current;
    }
    return terms_.mk_and(std::move(equalities));
  }

  core::TermId build_pairwise_equality_to_ite(core::TermId term, const smtlib::SExpr& ite_expr) {
    require_arity(ite_expr, 4, "ite");
    const core::TermId condition = build_term(ite_expr.list[1]);
    if (terms_.get(condition).sort != symbols_.bool_sort()) {
      throw UnsupportedInput("ite condition is not Boolean");
    }
    const core::TermId then_term = build_term(ite_expr.list[2]);
    const core::TermId else_term = build_term(ite_expr.list[3]);
    const core::SortId result_sort = terms_.get(then_term).sort;
    if (result_sort != terms_.get(else_term).sort) {
      throw UnsupportedInput("ite branch sort mismatch");
    }
    if (terms_.get(term).sort != result_sort) {
      throw UnsupportedInput("equality sort mismatch");
    }
    return terms_.mk_and(
        {terms_.mk_implies(condition, build_pairwise_equality(term, then_term)),
         terms_.mk_implies(terms_.mk_not(condition), build_pairwise_equality(term, else_term))});
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

  core::TermId bind_bool_argument_if_needed(core::TermId term) {
    const core::Term& node = terms_.get(term);
    if (node.sort != symbols_.bool_sort() || node.kind == core::TermKind::BoolValue ||
        node.kind == core::TermKind::Constant) {
      return term;
    }
    const core::TermId auxiliary = fresh_auxiliary_constant(symbols_.bool_sort(), "bool_arg");
    side_assertions_.push_back(
        terms_.mk_and({terms_.mk_implies(auxiliary, term), terms_.mk_implies(term, auxiliary)}));
    return auxiliary;
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
    bool occurs_positive = false;
    bool occurs_negative = false;
  };

  explicit BooleanClausifier(sat::IpasirUpBackend& backend, const core::TermManager& terms)
      : backend_(backend),
        terms_(terms),
        lit_by_term_(terms.size()),
        theory_atom_by_term_(terms.size(), kNoTheoryAtom) {}

  void assert_formula(core::TermId term) {
    if (terms_.get(term).sort != terms_.symbols().bool_sort()) {
      throw UnsupportedInput("assertion is not Boolean");
    }
    collect_theory_atom_polarities(term, true);
    assert_positive(term);
  }

  const std::vector<TheoryAtom>& theory_atoms() const { return theory_atoms_; }

 private:
  void collect_theory_atom_polarities(core::TermId term_id, bool positive) {
    const core::Term& term = terms_.get(term_id);
    switch (term.kind) {
      case core::TermKind::BoolValue:
        return;
      case core::TermKind::And:
      case core::TermKind::Or:
        for (const core::TermId child : term.children) {
          collect_theory_atom_polarities(child, positive);
        }
        return;
      case core::TermKind::Implies:
        collect_theory_atom_polarities(term.children.at(0), !positive);
        collect_theory_atom_polarities(term.children.at(1), positive);
        return;
      case core::TermKind::Not:
        collect_theory_atom_polarities(term.children.at(0), !positive);
        return;
      case core::TermKind::Constant:
        if (term.sort == terms_.symbols().bool_sort()) {
          record_theory_atom_polarity(term_id, positive);
        }
        return;
      case core::TermKind::Application:
        if (term.sort == terms_.symbols().bool_sort()) {
          record_theory_atom_polarity(term_id, positive);
        }
        return;
      case core::TermKind::Equality:
        record_theory_atom_polarity(term_id, positive);
        return;
    }
  }

  void record_theory_atom_polarity(core::TermId term_id, bool positive) {
    TheoryAtom& atom = theory_atom_for_term(term_id);
    if (positive) {
      atom.occurs_positive = true;
    } else {
      atom.occurs_negative = true;
    }
  }

  TheoryAtom& theory_atom_for_term(core::TermId term_id) {
    ensure_term_index(term_id);
    const std::size_t existing = theory_atom_by_term_[term_id];
    if (existing != kNoTheoryAtom) {
      return theory_atoms_[existing];
    }
    const sat::Lit lit = encode_theory_atom_lit(term_id);
    theory_atom_by_term_[term_id] = theory_atoms_.size();
    theory_atoms_.push_back(TheoryAtom{lit, term_id, false, false});
    return theory_atoms_.back();
  }

  void assert_positive(core::TermId term_id) {
    const core::Term& term = terms_.get(term_id);
    switch (term.kind) {
      case core::TermKind::BoolValue:
        if (!term.bool_value) {
          backend_.add_clause(sat::Clause{});
        }
        return;
      case core::TermKind::And:
        for (const core::TermId child : term.children) {
          assert_positive(child);
        }
        return;
      case core::TermKind::Or: {
        sat::Clause clause;
        clause.reserve(term.children.size());
        for (const core::TermId child : term.children) {
          const core::Term& child_term = terms_.get(child);
          if (child_term.kind == core::TermKind::BoolValue) {
            if (child_term.bool_value) {
              return;
            }
            continue;
          }
          clause.push_back(encode(child));
        }
        backend_.add_clause(clause);
        return;
      }
      case core::TermKind::Implies:
        backend_.add_clause(
            sat::Clause{negate(encode(term.children.at(0))), encode(term.children.at(1))});
        return;
      case core::TermKind::Not:
      case core::TermKind::Constant:
      case core::TermKind::Application:
      case core::TermKind::Equality:
        backend_.add_clause(sat::Clause{encode(term_id)});
        return;
    }
    throw UnsupportedInput("unsupported Boolean assertion");
  }

  sat::Lit encode(core::TermId term_id) {
    ensure_term_index(term_id);
    if (lit_by_term_[term_id].value != 0) {
      return lit_by_term_[term_id];
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
      lit_by_term_[term_id] = lit;
      return lit;
    }

    const sat::Lit lit{backend_.new_var()};
    lit_by_term_[term_id] = lit;

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
        return theory_atom_for_term(term_id).lit;
      case core::TermKind::Equality:
        return theory_atom_for_term(term_id).lit;
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Not:
        break;
    }
    throw UnsupportedInput("unsupported Boolean term");
  }

  sat::Lit atom_lit(core::TermId term_id) {
    ensure_term_index(term_id);
    if (lit_by_term_[term_id].value != 0) {
      return lit_by_term_[term_id];
    }
    const sat::Lit lit{backend_.new_var()};
    lit_by_term_[term_id] = lit;
    const core::Term& term = terms_.get(term_id);
    if (term.kind == core::TermKind::Constant && term.sort == terms_.symbols().bool_sort()) {
      return theory_atom_for_term(term_id).lit;
    }
    return lit;
  }

  sat::Lit encode_theory_atom_lit(core::TermId term_id) {
    ensure_term_index(term_id);
    if (lit_by_term_[term_id].value != 0) {
      return lit_by_term_[term_id];
    }
    const sat::Lit lit{backend_.new_var()};
    lit_by_term_[term_id] = lit;
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

  void ensure_term_index(core::TermId term_id) {
    if (term_id >= lit_by_term_.size()) {
      lit_by_term_.resize(term_id + 1);
      theory_atom_by_term_.resize(term_id + 1, kNoTheoryAtom);
    }
  }

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
  static constexpr std::size_t kNoTheoryAtom = static_cast<std::size_t>(-1);
  std::vector<sat::Lit> lit_by_term_;
  std::vector<std::size_t> theory_atom_by_term_;
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

void publish_theory_atom_purity_stats(const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                      util::Statistics& statistics);

CheckSatOutcome solve_boolean_conjunction(const core::TermManager& terms,
                                          const std::vector<core::TermId>& assertions,
                                          const CheckSatOptions& options,
                                          util::Statistics& statistics) {
  statistics.increment(kStatSolverBoolean);
  sat::CadicalBackend backend(options.cadical_options);
  BooleanClausifier clausifier(backend, terms);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }

  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  publish_theory_atom_purity_stats(clausifier.theory_atoms(), statistics);
  publish_sat_stats(backend.statistics(), statistics);
  statistics.increment(kStatSatSolveCalls);
  const CheckSatResult result = map_sat_result(backend.solve());
  publish_sat_stats(backend.statistics(), statistics);
  return CheckSatOutcome{result, "", statistics};
}

struct AssignedTheoryAtom {
  int lit = 0;
  bool value = false;
  core::TermId term = 0;
  bool filtered = false;
};

sat::Lit blocking_lit(const AssignedTheoryAtom& atom) {
  return atom.value ? sat::Lit{-atom.lit} : sat::Lit{atom.lit};
}

sat::Lit assigned_lit(const AssignedTheoryAtom& atom) {
  return atom.value ? sat::Lit{atom.lit} : sat::Lit{-atom.lit};
}

bool is_pure_positive(const BooleanClausifier::TheoryAtom& atom) {
  return atom.occurs_positive && !atom.occurs_negative;
}

bool is_pure_negative(const BooleanClausifier::TheoryAtom& atom) {
  return atom.occurs_negative && !atom.occurs_positive;
}

bool pure_theory_filter_enabled(PureTheoryFilterMode mode) {
  return mode != PureTheoryFilterMode::Off;
}

bool should_filter_assignment(PureTheoryFilterMode mode, const BooleanClausifier::TheoryAtom& atom,
                              bool value) {
  if (!pure_theory_filter_enabled(mode)) {
    return false;
  }
  return (is_pure_positive(atom) && !value) || (is_pure_negative(atom) && value);
}

AssignedTheoryAtom assigned_theory_atom(PureTheoryFilterMode mode,
                                        const BooleanClausifier::TheoryAtom& atom, bool value) {
  return AssignedTheoryAtom{atom.lit.value, value, atom.term,
                            should_filter_assignment(mode, atom, value)};
}

void publish_theory_atom_purity_stats(const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                      util::Statistics& statistics) {
  std::size_t positive = 0;
  std::size_t negative = 0;
  for (const BooleanClausifier::TheoryAtom& atom : atoms) {
    if (is_pure_positive(atom)) {
      ++positive;
    } else if (is_pure_negative(atom)) {
      ++negative;
    }
  }
  statistics.set(kStatPureTheoryAtomsPositive, positive);
  statistics.set(kStatPureTheoryAtomsNegative, negative);
}

void count_filtered_assignment(const AssignedTheoryAtom& atom, util::Statistics& statistics) {
  if (atom.filtered) {
    statistics.increment(kStatPureTheoryLiteralsFiltered);
  }
}

void increment_stat_if_nonzero(util::Statistics& statistics, util::StatKey key, std::size_t value) {
  if (value != 0) {
    statistics.increment(key, value);
  }
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
  increment_stat_if_nonzero(statistics, kStatEufIncrementalTerms, stats.terms);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalFlatDefinitions, stats.flat_definitions);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalMerges, stats.merges);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalPendingPops, stats.pending_pops);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalLookupProbes, stats.lookup_probes);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalLookupHits, stats.lookup_hits);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalCongruenceMerges,
                            stats.congruence_merges);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalDisequalityChecks,
                            stats.disequality_checks);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflicts, stats.conflicts);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCalls, stats.explain_calls);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCacheHits,
                            stats.explain_cache_hits);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainLength, stats.explain_length);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalBacktracks, stats.backtracks);
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
      positive_delta(lhs.explain_calls, rhs.explain_calls),
      positive_delta(lhs.explain_cache_hits, rhs.explain_cache_hits),
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
    if (atom.filtered) {
      continue;
    }
    const core::Term& term = terms.get(atom.term);
    if (term.kind == core::TermKind::Equality && !atom.value) {
      continue;
    }
    if (!add_explained_theory_equality(explained, terms, atom, true_term, false_term)) {
      throw UnsupportedInput("unsupported theory atom");
    }
  }

  const AssignedTheoryAtom& candidate = atoms[candidate_index];
  if (candidate.filtered) {
    return std::nullopt;
  }
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
    if (atom.source.filtered) {
      continue;
    }
    if (atom.equality && !atom.source.value) {
      continue;
    }
    assert_flat_assigned_atom(closure, atom, true_flat, false_flat);
  }

  if (flat_atoms[candidate_index].source.filtered) {
    if (statistics != nullptr) {
      add_incremental_stats(*statistics, closure.stats());
    }
    return std::nullopt;
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
                                 PureTheoryFilterMode pure_theory_filter,
                                 util::Statistics& statistics)
      : terms_(terms),
        pure_theory_filter_(pure_theory_filter),
        flat_(terms),
        closure_(0),
        statistics_(statistics) {
    true_flat_ = flat_.flatten(true_term);
    false_flat_ = flat_.flatten(false_term);
    flat_atoms_.reserve(theory_atoms.size());
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      flat_atom_by_var_.emplace(var, flat_atoms_.size());
      flat_atoms_.push_back(flatten_assigned_atom(
          flat_, terms_, AssignedTheoryAtom{atom.lit.value, false, atom.term}, false_flat_));
    }

    initialize_base_closure();
    publish_stats();
  }

  void push_level() { checkpoints_.push_back(Checkpoint{assignments_.size(), std::nullopt}); }

  void pop_to_level(std::size_t new_level) {
    if (checkpoints_.size() <= new_level) {
      return;
    }
    const std::size_t assignment_count = checkpoints_[new_level].assignment_count;
    std::optional<std::size_t> closure_checkpoint;
    for (std::size_t index = new_level; index < checkpoints_.size(); ++index) {
      if (checkpoints_[index].closure_checkpoint.has_value()) {
        closure_checkpoint = checkpoints_[index].closure_checkpoint;
        break;
      }
    }
    checkpoints_.resize(new_level);
    assignments_.resize(assignment_count);
    if (closure_checkpoint.has_value()) {
      closure_.pop(closure_checkpoint.value());
      last_published_stats_ = closure_.stats();
      ++accumulated_stats_.backtracks;
    }
  }

  void assert_assignment(const AssignedTheoryAtom& assignment) {
    if (assignment.filtered) {
      return;
    }
    std::optional<FlatAssignedTheoryAtom> flat_atom = flat_atom_for_lit(assignment.lit);
    if (!flat_atom.has_value()) {
      return;
    }
    ensure_current_level_checkpoint();
    assignments_.push_back(assignment);
    flat_atom->source.value = assignment.value;
    flat_atom->source.filtered = assignment.filtered;
    assert_flat_assigned_atom(closure_, flat_atom.value(), true_flat_, false_flat_);
    publish_stats();
  }

  std::optional<sat::Clause> explain_candidate(const BooleanClausifier::TheoryAtom& atom,
                                               bool value) {
    if (should_filter_assignment(pure_theory_filter_, atom, value)) {
      return std::nullopt;
    }
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

  void drain_changed_terms(std::vector<theory::equality::FlatTermId>& out) {
    closure_.drain_changed_terms(out);
  }

  const std::vector<FlatAssignedTheoryAtom>& flat_atoms() const { return flat_atoms_; }

  std::size_t flat_term_count() const { return flat_.size(); }

  std::optional<sat::Clause> conflict() const { return closure_.conflict(); }

  void flush_stats() {
    add_incremental_stats(statistics_, accumulated_stats_);
    accumulated_stats_ = {};
  }

 private:
  void initialize_base_closure() {
    closure_.ensure_node_count(flat_.size());
    for (const theory::equality::FlatDefinition& definition : flat_.definitions()) {
      closure_.add_definition(definition);
    }
    closure_.assert_distinct(true_flat_, false_flat_);
    std::vector<theory::equality::FlatTermId> ignored_changed_terms;
    closure_.drain_changed_terms(ignored_changed_terms);
  }

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
    accumulate_stats(stats_delta(current, last_published_stats_));
    last_published_stats_ = current;
  }

  void accumulate_stats(const theory::equality::IncrementalCongruenceClosureStats& stats) {
    accumulated_stats_.terms += stats.terms;
    accumulated_stats_.flat_definitions += stats.flat_definitions;
    accumulated_stats_.merges += stats.merges;
    accumulated_stats_.pending_pops += stats.pending_pops;
    accumulated_stats_.lookup_probes += stats.lookup_probes;
    accumulated_stats_.lookup_hits += stats.lookup_hits;
    accumulated_stats_.congruence_merges += stats.congruence_merges;
    accumulated_stats_.disequality_checks += stats.disequality_checks;
    accumulated_stats_.conflicts += stats.conflicts;
    accumulated_stats_.explain_calls += stats.explain_calls;
    accumulated_stats_.explain_cache_hits += stats.explain_cache_hits;
    accumulated_stats_.explain_length += stats.explain_length;
    accumulated_stats_.backtracks += stats.backtracks;
  }

  void ensure_current_level_checkpoint() {
    if (checkpoints_.empty()) {
      return;
    }
    Checkpoint& checkpoint = checkpoints_.back();
    if (!checkpoint.closure_checkpoint.has_value()) {
      checkpoint.closure_checkpoint = closure_.push();
    }
  }

  const core::TermManager& terms_;
  PureTheoryFilterMode pure_theory_filter_ = PureTheoryFilterMode::Auto;
  struct Checkpoint {
    std::size_t assignment_count = 0;
    std::optional<std::size_t> closure_checkpoint;
  };

  theory::equality::FlatTermIndex flat_;
  theory::equality::FlatCongruenceClosure closure_;
  util::Statistics& statistics_;
  theory::equality::FlatTermId true_flat_ = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId false_flat_ = theory::equality::kInvalidFlatTermId;
  std::vector<FlatAssignedTheoryAtom> flat_atoms_;
  std::unordered_map<int, std::size_t> flat_atom_by_var_;
  std::vector<AssignedTheoryAtom> assignments_;
  std::vector<Checkpoint> checkpoints_;
  theory::equality::IncrementalCongruenceClosureStats accumulated_stats_;
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
                      const CheckSatOptions& options, bool adaptive_propagation,
                      util::Statistics& statistics)
      : terms_(terms),
        theory_atoms_(theory_atoms),
        true_term_(true_term),
        false_term_(false_term),
        options_(options),
        adaptive_propagation_(adaptive_propagation),
        statistics_(statistics) {
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      observed_vars_.insert(var);
      atom_by_var_.emplace(var, &atom);
    }
    if (options_.euf_engine == EufEngine::Incremental) {
      incremental_state_.emplace(terms_, theory_atoms_, true_term_, false_term_,
                                 options_.pure_theory_filter, statistics_);
      build_candidate_index();
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
          AssignedTheoryAtom assigned =
              assigned_theory_atom(options_.pure_theory_filter, *atom, atom_value);
          count_filtered_assignment(assigned, statistics_);
          incremental_state_->assert_assignment(assigned);
          enqueue_changed_candidates();
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
    candidate_queue_.clear();
    queued_candidate_indices_.clear();
    external_clause_.clear();
    external_clause_index_ = 0;
  }

  bool cb_check_found_model(const std::vector<int>& /*model*/) override { return true; }

  void flush_incremental_stats() {
    if (incremental_state_.has_value()) {
      incremental_state_->flush_stats();
    }
  }

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
    const auto found = atom_by_var_.find(var);
    return found == atom_by_var_.end() ? nullptr : found->second;
  }

  void build_candidate_index() {
    if (!incremental_state_.has_value()) {
      return;
    }
    candidate_indices_by_flat_term_.assign(incremental_state_->flat_term_count(), {});
    const std::vector<FlatAssignedTheoryAtom>& flat_atoms = incremental_state_->flat_atoms();
    for (std::size_t atom_index = 0; atom_index < flat_atoms.size(); ++atom_index) {
      const FlatAssignedTheoryAtom& atom = flat_atoms[atom_index];
      if (atom.lhs < candidate_indices_by_flat_term_.size()) {
        candidate_indices_by_flat_term_[atom.lhs].push_back(atom_index);
      }
      if (atom.rhs != atom.lhs && atom.rhs < candidate_indices_by_flat_term_.size()) {
        candidate_indices_by_flat_term_[atom.rhs].push_back(atom_index);
      }
    }
  }

  void enqueue_candidate(std::size_t atom_index) {
    if (atom_index >= theory_atoms_.size()) {
      return;
    }
    if (queued_candidate_indices_.insert(atom_index).second) {
      candidate_queue_.push_back(atom_index);
    }
  }

  void enqueue_changed_candidates() {
    if (!incremental_state_.has_value()) {
      return;
    }
    changed_terms_scratch_.clear();
    incremental_state_->drain_changed_terms(changed_terms_scratch_);
    for (const theory::equality::FlatTermId term : changed_terms_scratch_) {
      if (term >= candidate_indices_by_flat_term_.size()) {
        continue;
      }
      for (const std::size_t atom_index : candidate_indices_by_flat_term_[term]) {
        enqueue_candidate(atom_index);
      }
    }
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
      AssignedTheoryAtom assigned =
          assigned_theory_atom(options_.pure_theory_filter, atom, atom_value);
      atoms.push_back(assigned);
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
    atoms.push_back(assigned_theory_atom(options_.pure_theory_filter, *atom, value));
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
    const std::size_t round = statistics_.get(kStatEufPropagationRounds);
    if (round < next_propagation_round_) {
      return;
    }
    next_propagation_round_ = round + propagation_interval_;
    const std::vector<AssignedTheoryAtom> assigned = current_assigned_atoms();
    const std::size_t theory_atoms = theory_atoms_.size();
    if (theory_atoms > 0 &&
        static_cast<double>(assigned.size()) / static_cast<double>(theory_atoms) >
            options_.euf_propagation_assign_threshold) {
      statistics_.increment(kStatEufPropagationHitRateCutoffs);
      backoff_propagation_interval();
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
    auto try_atom = [&](const BooleanClausifier::TheoryAtom& atom) {
      if (candidate_limit.has_value() && inspected >= candidate_limit.value()) {
        statistics_.increment(kStatEufPropagationBudgetCutoffs);
        return false;
      }
      if (delivery_limit.has_value() && delivered >= delivery_limit.value()) {
        statistics_.increment(kStatEufPropagationBudgetCutoffs);
        return false;
      }
      if (inspected >= kMinInspectionsBeforeHitRateCutoff && delivered == 0 &&
          options_.euf_propagation_budget.kind == AutoBudget::Kind::Auto) {
        statistics_.increment(kStatEufPropagationHitRateCutoffs);
        backoff_propagation_interval();
        return false;
      }

      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (assignment_by_var_.contains(var) || pending_vars_.contains(var)) {
        return true;
      }
      ++inspected;
      if (try_online_candidate(assigned, atom, true) ||
          try_online_candidate(assigned, atom, false)) {
        ++delivered;
      }
      return true;
    };

    const bool had_indexed_candidates = !candidate_queue_.empty();
    while (!candidate_queue_.empty()) {
      const std::size_t atom_index = candidate_queue_.front();
      candidate_queue_.pop_front();
      queued_candidate_indices_.erase(atom_index);
      if (!try_atom(theory_atoms_[atom_index])) {
        break;
      }
    }

    if (!had_indexed_candidates || (delivered == 0 && candidate_queue_.empty())) {
      for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
        if (!try_atom(atom)) {
          break;
        }
      }
    }
    if (delivered == 0) {
      backoff_propagation_interval();
    } else {
      propagation_interval_ = options_.euf_propagation_interval;
    }
    statistics_.increment(kStatEufPropagationCandidates, inspected);
  }

  void backoff_propagation_interval() {
    if (!adaptive_propagation_) {
      return;
    }
    constexpr std::size_t kMaxAdaptivePropagationInterval = 1024;
    propagation_interval_ = std::min(kMaxAdaptivePropagationInterval, propagation_interval_ * 2);
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
      atoms.push_back(assigned_theory_atom(options_.pure_theory_filter, atom, value));
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
  bool adaptive_propagation_ = false;
  util::Statistics& statistics_;
  std::unordered_set<int> observed_vars_;
  std::unordered_map<int, const BooleanClausifier::TheoryAtom*> atom_by_var_;
  std::unordered_map<int, bool> assignment_by_var_;
  std::vector<int> trail_;
  std::vector<std::size_t> level_starts_{0};
  std::deque<int> pending_;
  std::unordered_set<int> pending_vars_;
  std::deque<std::size_t> candidate_queue_;
  std::unordered_set<std::size_t> queued_candidate_indices_;
  std::vector<std::vector<std::size_t>> candidate_indices_by_flat_term_;
  std::vector<theory::equality::FlatTermId> changed_terms_scratch_;
  std::unordered_map<int, Reason> reasons_;
  std::optional<IncrementalFlatAssignmentState> incremental_state_;
  sat::Clause external_clause_;
  std::size_t external_clause_index_ = 0;
  std::size_t propagation_interval_ = options_.euf_propagation_interval;
  std::size_t next_propagation_round_ = 1;
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
    if (atom.filtered) {
      continue;
    }
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
    atom.source.filtered = atoms[i].filtered;
    if (atom.source.filtered) {
      continue;
    }
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
  sat::CadicalBackend backend(options.cadical_options);
  BooleanClausifier clausifier(backend, terms);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  statistics.set(kStatPureTheoryFilterEnabled,
                 pure_theory_filter_enabled(options.pure_theory_filter) ? 1 : 0);
  publish_theory_atom_purity_stats(clausifier.theory_atoms(), statistics);
  const EufPropagationMode euf_propagation =
      resolve_euf_propagation_mode(options, clausifier.theory_atoms().size(), statistics);
  CheckSatOptions resolved_options = options;
  resolved_options.euf_propagation = euf_propagation;
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    backend.phase(sat::Lit{-atom.lit.value});
    statistics.increment(kStatSatTheoryNegativePhase);
  }
  publish_sat_stats(backend.statistics(), statistics);

  const core::TermId true_term = terms.mk_bool(true);
  const core::TermId false_term = terms.mk_bool(false);
  std::unordered_set<std::string> learned_model_propagations;
  std::optional<PreparedFlatTheory> prepared_flat_theory;
  if (options.euf_engine == EufEngine::Incremental) {
    prepared_flat_theory.emplace(terms, clausifier.theory_atoms(), true_term, false_term);
  }

#if PRIMO_HAVE_IPASIR
  std::optional<OnlineEufPropagator> online_propagator;
  if (euf_propagation == EufPropagationMode::Online ||
      euf_propagation == EufPropagationMode::Conflict) {
    online_propagator.emplace(terms, clausifier.theory_atoms(), true_term, false_term,
                              resolved_options, options.euf_propagation == EufPropagationMode::Auto,
                              statistics);
    backend.connect_external_propagator(&online_propagator.value());
    for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      backend.add_observed_var(var);
    }
  }
#else
  if (euf_propagation == EufPropagationMode::Online ||
      euf_propagation == EufPropagationMode::Conflict) {
    return CheckSatOutcome{CheckSatResult::Unsupported,
                           "online EUF conflict/propagation requires CaDiCaL external propagators",
                           statistics};
  }
#endif

  while (true) {
    statistics.increment(kStatSatSolveCalls);
    const sat::SatResult sat_result = backend.solve();
    if (online_propagator.has_value()) {
      online_propagator->flush_incremental_stats();
    }
    publish_sat_stats(backend.statistics(), statistics);
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
      AssignedTheoryAtom assigned =
          assigned_theory_atom(options.pure_theory_filter, atom, atom_value);
      count_filtered_assignment(assigned, statistics);
      assigned_atoms.push_back(assigned);
    }

    if (euf_propagation == EufPropagationMode::Model) {
      statistics.increment(kStatEufPropagationRounds);
      ModelPropagationResult propagation =
          learn_model_propagations(terms, assigned_atoms, true_term, false_term, resolved_options,
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
      publish_sat_stats(backend.statistics(), statistics);
    }

    std::optional<sat::Clause> conflict_clause =
        options.euf_engine == EufEngine::Incremental
            ? find_incremental_theory_conflict(prepared_flat_theory.value(), assigned_atoms,
                                               statistics)
            : find_theory_conflict(terms, assigned_atoms, true_term, false_term);
    if (!conflict_clause.has_value()) {
      publish_sat_stats(backend.statistics(), statistics);
      return CheckSatOutcome{CheckSatResult::Sat, "", statistics};
    }
    statistics.increment(kStatTheoryConflicts);
    if (conflict_clause->empty()) {
      publish_sat_stats(backend.statistics(), statistics);
      return CheckSatOutcome{CheckSatResult::Unsat, "", statistics};
    }
    backend.add_clause(conflict_clause.value());
    publish_sat_stats(backend.statistics(), statistics);
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
  statistics.set(kStatPreprocessingAdaptivePasses, 0);
  statistics.set(kStatPreprocessingTermSizeInitial, 0);
  statistics.set(kStatPreprocessingTermSizeFinal, 0);
  statistics.set(kStatEqBridgeEqualities, 0);
  statistics.set(kStatEqBridgeSkippedOrNodes, 0);
  statistics.set(kStatBooleanUnitFacts, 0);
  statistics.set(kStatBooleanUnitRewrites, 0);
  statistics.set(kStatForcedAtomFacts, 0);
  statistics.set(kStatForcedAtomDisequalities, 0);
  statistics.set(kStatForcedAtomRewrites, 0);
  statistics.set(kStatUnitEqualityConsequences, 0);
  statistics.set(kStatUnitEqualitySubstitutionUnits, 0);
  statistics.set(kStatUnitEqualitySubstitutions, 0);
  statistics.set(kStatUnitEqualitySubstitutionAccepted, 0);
  statistics.set(kStatUnitEqualitySubstitutionRejected, 0);
  statistics.set(kStatSolvedConstantCandidates, 0);
  statistics.set(kStatSolvedConstantAccepted, 0);
  statistics.set(kStatSolvedConstantRewrites, 0);
  statistics.set(kStatSolvedConstantRejectedCycles, 0);
  statistics.set(kStatSolvedConstantRejectedBlowups, 0);
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
  statistics.set(kStatAutoLargeAssertionSkip, 0);
  statistics.set(kStatSatTheoryNegativePhase, 0);
  statistics.set(kStatSatVars, 0);
  statistics.set(kStatSatClauses, 0);
  statistics.set(kStatSatClauseLits, 0);
  statistics.set(kStatSatUnitClauses, 0);
  statistics.set(kStatSatBinaryClauses, 0);
  statistics.set(kStatSolverBoolean, 0);
  statistics.set(kStatSolverQfUf, 0);
  statistics.set(kStatSatSolveCalls, 0);
  statistics.set(kStatTheoryAtoms, 0);
  statistics.set(kStatPureTheoryFilterEnabled, 0);
  statistics.set(kStatPureTheoryAtomsPositive, 0);
  statistics.set(kStatPureTheoryAtomsNegative, 0);
  statistics.set(kStatPureTheoryLiteralsFiltered, 0);
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
  statistics.set(kStatEufPropagationAutoOnline, 0);
  statistics.set(kStatEufPropagationAutoConflict, 0);
  statistics.set(kStatEufPropagationAutoOff, 0);
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
    const bool auto_large_assertion_skip = options.qf_uf_preprocess == QfUfPreprocessMode::Auto &&
                                           assertions.size() > kAutoPreprocessLargeAssertionLimit;
    const bool auto_strong = options.qf_uf_preprocess == QfUfPreprocessMode::Auto &&
                             assertions.size() > 1 && !auto_large_assertion_skip;
    if (auto_single_assertion) {
      statistics.increment(kStatAutoSingleAssertion);
    }
    if (auto_strong) {
      statistics.increment(kStatAutoStrong);
    }
    if (auto_large_assertion_skip) {
      statistics.increment(kStatAutoLargeAssertionSkip);
    }
    const bool strong_preprocessing =
        options.qf_uf_preprocess == QfUfPreprocessMode::Strong || auto_strong;
    const bool preprocessing_enabled =
        options.qf_uf_preprocess != QfUfPreprocessMode::Off && !auto_large_assertion_skip;
    const bool use_eq_bridge = options.eq_bridge || strong_preprocessing || auto_single_assertion;
    const bool use_finite_domain_evaluation = false;
    const bool use_finite_domain_amo = strong_preprocessing || auto_single_assertion;
    const bool use_finite_domain_equality_definitions =
        options.qf_uf_preprocess == QfUfPreprocessMode::Strong;
    const bool use_ground_congruence_definitions = options.ground_congruence;
    const bool use_ground_unit_substitution = options.ground_unit_substitution;
    const bool use_guarded_ground_unit_substitution =
        options.guarded_ground_unit_substitution && !options.ground_unit_substitution;
    const bool use_symmetry_breaking = preprocessing_enabled && options.uf_symmetry_breaking;
    const std::size_t preprocessing_passes =
        (strong_preprocessing || auto_single_assertion)
            ? std::max<std::size_t>(options.preprocessing_passes, 3)
            : options.preprocessing_passes;
    const std::size_t adaptive_shrink_percent =
        std::min<std::size_t>(options.preprocessing_adaptive_shrink_percent, 100);
    std::size_t preprocessing_pass_limit = preprocessing_passes;

    if (use_symmetry_breaking) {
      UfSymmetryBreaker symmetry_breaker(frontend.terms());
      UfSymmetryBreakingResult symmetry = symmetry_breaker.break_symmetries(assertions);
      assertions.insert(assertions.end(), symmetry.constraints.begin(), symmetry.constraints.end());
      statistics.increment(kStatUfSymmetryAcceptedSets, symmetry.accepted_sets);
      statistics.increment(kStatUfSymmetryValues, symmetry.values);
      statistics.increment(kStatUfSymmetryTerms, symmetry.terms);
      statistics.increment(kStatUfSymmetryConstraints, symmetry.constraints.size());
      statistics.increment(kStatUfSymmetryRejectedNoninvariant, symmetry.rejected_noninvariant);
    }
    statistics.set(kStatPreprocessingTermSizeInitial,
                   reachable_term_count(frontend.terms(), assertions));
    if (auto_large_assertion_skip && options.solved_constant_substitution) {
      SolvedConstantSubstitutionResult solved_constants =
          substitute_solved_constants(frontend.terms(), assertions);
      assertions = std::move(solved_constants.assertions);
      statistics.increment(kStatSolvedConstantCandidates, solved_constants.candidates);
      statistics.increment(kStatSolvedConstantAccepted, solved_constants.accepted);
      statistics.increment(kStatSolvedConstantRewrites, solved_constants.rewrites);
      statistics.increment(kStatSolvedConstantRejectedCycles, solved_constants.rejected_cycles);
      statistics.increment(kStatSolvedConstantRejectedBlowups, solved_constants.rejected_blowups);
    }
    for (std::size_t pass = 0; preprocessing_enabled && pass < preprocessing_pass_limit; ++pass) {
      const std::vector<core::TermId> before = assertions;
      const std::size_t before_term_size = reachable_term_count(frontend.terms(), before);
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
      BooleanUnitPropagationResult boolean_units =
          propagate_boolean_units(frontend.terms(), assertions);
      assertions = std::move(boolean_units.assertions);
      statistics.increment(kStatBooleanUnitFacts, boolean_units.units);
      statistics.increment(kStatBooleanUnitRewrites, boolean_units.rewrites);
      ForcedAtomPropagationResult forced_atoms =
          propagate_forced_atoms(frontend.terms(), assertions);
      assertions = std::move(forced_atoms.assertions);
      statistics.increment(kStatForcedAtomFacts, forced_atoms.units);
      statistics.increment(kStatForcedAtomDisequalities, forced_atoms.disequalities);
      statistics.increment(kStatForcedAtomRewrites, forced_atoms.rewrites);
      if (options.solved_constant_substitution) {
        SolvedConstantSubstitutionResult solved_constants =
            substitute_solved_constants(frontend.terms(), assertions);
        assertions = std::move(solved_constants.assertions);
        statistics.increment(kStatSolvedConstantCandidates, solved_constants.candidates);
        statistics.increment(kStatSolvedConstantAccepted, solved_constants.accepted);
        statistics.increment(kStatSolvedConstantRewrites, solved_constants.rewrites);
        statistics.increment(kStatSolvedConstantRejectedCycles, solved_constants.rejected_cycles);
        statistics.increment(kStatSolvedConstantRejectedBlowups, solved_constants.rejected_blowups);
      }
      if (use_ground_unit_substitution || use_guarded_ground_unit_substitution) {
        GroundEqualitySubstitutionResult ground_substitution =
            substitute_ground_unit_equalities(frontend.terms(), assertions);
        const bool accept_substitution =
            use_ground_unit_substitution ||
            accept_guarded_ground_substitution(assertions, ground_substitution.assertions,
                                               ground_substitution, frontend.terms());
        if (accept_substitution) {
          assertions = std::move(ground_substitution.assertions);
          statistics.increment(kStatUnitEqualitySubstitutionUnits, ground_substitution.units);
          statistics.increment(kStatUnitEqualitySubstitutions, ground_substitution.rewrites);
          statistics.increment(kStatUnitEqualitySubstitutionAccepted);
        } else if (ground_substitution.rewrites > 0) {
          statistics.increment(kStatUnitEqualitySubstitutionRejected);
        }
      }
      std::size_t unit_consequences = 0;
      assertions = propagate_unit_equalities(frontend.terms(), assertions, &unit_consequences);
      statistics.increment(kStatUnitEqualityConsequences, unit_consequences);
      if (assertions == before) {
        break;
      }
      statistics.increment(kStatPreprocessingChangedPasses);
      const std::size_t after_term_size = reachable_term_count(frontend.terms(), assertions);
      const bool shrank_enough =
          after_term_size < before_term_size &&
          after_term_size * 100 <= before_term_size * (100 - adaptive_shrink_percent);
      const std::size_t max_adaptive_limit =
          preprocessing_passes + options.preprocessing_adaptive_extra_passes;
      if (pass + 1 == preprocessing_pass_limit && preprocessing_pass_limit < max_adaptive_limit &&
          shrank_enough) {
        ++preprocessing_pass_limit;
        statistics.increment(kStatPreprocessingAdaptivePasses);
      }
    }
    statistics.set(kStatAssertionsFinal, assertions.size());
    statistics.set(kStatPreprocessingTermSizeFinal,
                   reachable_term_count(frontend.terms(), assertions));
    for (const core::TermId assertion : assertions) {
      const core::Term& node = frontend.terms().get(assertion);
      if (node.kind == core::TermKind::BoolValue && !node.bool_value) {
        return CheckSatOutcome{CheckSatResult::Unsat, "", statistics};
      }
    }
    if (assertions.empty() && !options.preprocess_only) {
      return CheckSatOutcome{CheckSatResult::Sat, "", statistics};
    }
    if (options.preprocess_only) {
      return CheckSatOutcome{CheckSatResult::Unknown, "preprocess-only", statistics};
    }

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
      return solve_boolean_conjunction(frontend.terms(), assertions, options, statistics);
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
