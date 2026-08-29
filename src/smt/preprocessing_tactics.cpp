#include "smt/preprocessing_tactics.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "smt/formula_metrics.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/preprocessing.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

void append_boolean_model_assignments(
    ModelReconstruction& reconstruction,
    const std::vector<std::pair<core::TermId, bool>>& assignments) {
  reconstruction.model_assignments.reserve(reconstruction.model_assignments.size() +
                                           assignments.size());
  for (const auto& [term, value] : assignments) {
    reconstruction.model_assignments.push_back(TheoryModelAssignment{term, value, false});
  }
}

void append_equality_model_assignments(
    core::TermManager& terms, ModelReconstruction& reconstruction,
    const std::vector<std::pair<core::TermId, core::TermId>>& equalities) {
  reconstruction.model_assignments.reserve(reconstruction.model_assignments.size() +
                                           equalities.size());
  for (const auto& [lhs, rhs] : equalities) {
    reconstruction.model_assignments.push_back(
        TheoryModelAssignment{terms.mk_eq(lhs, rhs), true, false, true});
  }
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

enum class RoundPhase {
  Count,
  EqualityBridge,
  FiniteDomainEvaluation,
  CongruenceDefinitions,
  BooleanUnits,
  ForcedAtoms,
  SolvedConstants,
  GroundUnitSubstitution,
  UnitEqualities,
};

Goal run_phase(TacticContext& context, const Goal& goal, const PreprocessingRoundOptions& options,
               RoundPhase phase) {
  core::TermManager& terms = context.terms();
  util::Statistics& statistics = context.statistics();
  std::vector<core::TermId> assertions = goal.assertions();
  ModelReconstruction reconstruction = goal.reconstruction();
  if (phase == RoundPhase::Count) {
    statistics.increment(util::StatKey::PreprocessingPasses);
    return goal;
  }

  if (phase == RoundPhase::EqualityBridge && options.use_eq_bridge) {
    EqualityBridge bridge(terms);
    EqualityBridgeResult result = bridge.bridge(assertions);
    assertions.insert(assertions.end(), result.equalities.begin(), result.equalities.end());
    statistics.increment(util::StatKey::EqBridgeEqualities, result.equalities.size());
    statistics.increment(util::StatKey::EqBridgeSkippedOrNodes, result.skipped_or_nodes);
  }
  if (phase == RoundPhase::FiniteDomainEvaluation && options.use_finite_domain_evaluation) {
    FiniteDomainEvaluator evaluator(terms);
    FiniteDomainEvaluationResult result = evaluator.simplify(assertions);
    assertions = std::move(result.assertions);
    statistics.increment(util::StatKey::FiniteDomainEvaluatedEqualities,
                         result.equalities_simplified);
    statistics.increment(util::StatKey::FiniteDomainEvaluationCutoffs, result.enumeration_cutoffs);
  }
  if (phase == RoundPhase::CongruenceDefinitions && options.use_ground_congruence_definitions) {
    CongruenceDefinitionGenerator generator(terms);
    CongruenceDefinitionResult result = generator.generate(assertions);
    assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
    statistics.increment(util::StatKey::GroundCongruenceClauses, result.clauses.size());
    statistics.increment(util::StatKey::GroundCongruenceSkippedGroups, result.skipped_groups);
  }

  if (phase == RoundPhase::BooleanUnits) {
    BooleanUnitPropagationResult result = propagate_boolean_units(terms, assertions);
    assertions = std::move(result.assertions);
    if (options.produce_model) {
      append_boolean_model_assignments(reconstruction, result.assignments);
    }
    statistics.increment(util::StatKey::BooleanUnitFacts, result.units);
    statistics.increment(util::StatKey::BooleanUnitRewrites, result.rewrites);
    statistics.increment(util::StatKey::BooleanUnitMemoHits, result.memo_hits);
    statistics.increment(util::StatKey::BooleanUnitMemoMisses, result.memo_misses);
  }

  if (phase == RoundPhase::ForcedAtoms) {
    ForcedAtomPropagationResult result = propagate_forced_atoms(terms, assertions);
    assertions = std::move(result.assertions);
    if (options.produce_model) {
      append_boolean_model_assignments(reconstruction, result.assignments);
    }
    statistics.increment(util::StatKey::ForcedAtomFacts, result.units);
    statistics.increment(util::StatKey::ForcedAtomDisequalities, result.disequalities);
    statistics.increment(util::StatKey::ForcedAtomRewrites, result.rewrites);
    statistics.increment(util::StatKey::ForcedAtomMemoHits, result.memo_hits);
    statistics.increment(util::StatKey::ForcedAtomMemoMisses, result.memo_misses);
    statistics.increment(util::StatKey::EqualityQueryNodes, result.query_nodes);
    statistics.increment(util::StatKey::EqualityQueryMemoHits, result.query_memo_hits);
    statistics.increment(util::StatKey::EqualityQueryAddTermRequests, result.add_term_requests);
    statistics.increment(util::StatKey::EqualityQueryAddTermSeenHits, result.add_term_seen_hits);
    reconstruction.forced_equalities.insert(reconstruction.forced_equalities.end(),
                                            result.equality_pairs.begin(),
                                            result.equality_pairs.end());
    reconstruction.forced_disequalities.insert(reconstruction.forced_disequalities.end(),
                                               result.disequality_pairs.begin(),
                                               result.disequality_pairs.end());
  }

  if (phase == RoundPhase::SolvedConstants && options.solved_constant_substitution) {
    SolvedConstantSubstitutionResult solved =
        substitute_solved_constants(terms, assertions, options.gaussian_elimination);
    assertions = std::move(solved.assertions);
    if (options.produce_model) {
      append_equality_model_assignments(terms, reconstruction, solved.substitutions);
    }
    statistics.increment(util::StatKey::SolvedConstantCandidates, solved.candidates);
    statistics.increment(util::StatKey::SolvedConstantAccepted, solved.accepted);
    statistics.increment(util::StatKey::SolvedConstantRewrites, solved.rewrites);
    statistics.increment(util::StatKey::SolvedConstantRejectedCycles, solved.rejected_cycles);
    statistics.increment(util::StatKey::SolvedConstantRejectedBlowups, solved.rejected_blowups);
    statistics.increment(util::StatKey::SolvedConstantGaussianCandidates,
                         solved.gaussian_candidates);
  }

  if (phase == RoundPhase::GroundUnitSubstitution &&
      (options.use_ground_unit_substitution || options.use_guarded_ground_unit_substitution)) {
    GroundEqualitySubstitutionResult substitution =
        substitute_ground_unit_equalities(terms, assertions);
    const bool accept = options.use_ground_unit_substitution ||
                        accept_guarded_ground_substitution(assertions, substitution.assertions,
                                                           substitution, terms);
    if (accept) {
      assertions = std::move(substitution.assertions);
      if (options.produce_model) {
        append_equality_model_assignments(terms, reconstruction, substitution.substitutions);
      }
      statistics.increment(util::StatKey::UnitEqualitySubstitutionUnits, substitution.units);
      statistics.increment(util::StatKey::UnitEqualitySubstitutions, substitution.rewrites);
      statistics.increment(util::StatKey::UnitEqualitySubstitutionAccepted);
    } else if (substitution.rewrites > 0) {
      statistics.increment(util::StatKey::UnitEqualitySubstitutionRejected);
    }
  }

  if (phase == RoundPhase::UnitEqualities) {
    UnitEqualityPropagationStats result;
    assertions = propagate_unit_equalities(terms, assertions, &result);
    statistics.increment(util::StatKey::UnitEqualityConsequences, result.consequences_added);
    statistics.increment(util::StatKey::EqualityQueryNodes, result.query_nodes);
    statistics.increment(util::StatKey::EqualityQueryMemoHits, result.query_memo_hits);
    statistics.increment(util::StatKey::EqualityQueryAddTermRequests, result.add_term_requests);
    statistics.increment(util::StatKey::EqualityQueryAddTermSeenHits, result.add_term_seen_hits);
  }
  return goal.with_assertions(std::move(assertions)).with_reconstruction(std::move(reconstruction));
}

Tactic make_phase_tactic(std::string name, RoundPhase phase, PreprocessingRoundOptions options) {
  return Tactic(std::move(name), [phase, options](TacticContext& context, const AnyOf& input) {
    std::vector<Goal> output;
    output.reserve(input.size());
    for (const Goal& goal : input.alternatives()) {
      output.push_back(run_phase(context, goal, options, phase));
    }
    return AnyOf(std::move(output));
  });
}

}  // namespace

Tactic make_preprocessing_round(PreprocessingRoundOptions options) {
  std::vector<Tactic> phases;
  phases.push_back(make_phase_tactic("preprocessing-pass", RoundPhase::Count, options));
  if (options.use_eq_bridge) {
    phases.push_back(make_phase_tactic("equality-bridge", RoundPhase::EqualityBridge, options));
  }
  if (options.use_finite_domain_evaluation) {
    phases.push_back(
        make_phase_tactic("finite-domain-evaluation", RoundPhase::FiniteDomainEvaluation, options));
  }
  if (options.use_ground_congruence_definitions) {
    phases.push_back(
        make_phase_tactic("congruence-definitions", RoundPhase::CongruenceDefinitions, options));
  }
  phases.push_back(
      make_phase_tactic("boolean-unit-propagation", RoundPhase::BooleanUnits, options));
  phases.push_back(make_phase_tactic("forced-atom-propagation", RoundPhase::ForcedAtoms, options));
  if (options.solved_constant_substitution) {
    phases.push_back(
        make_phase_tactic("solved-constant-substitution", RoundPhase::SolvedConstants, options));
  }
  if (options.use_ground_unit_substitution || options.use_guarded_ground_unit_substitution) {
    phases.push_back(
        make_phase_tactic("ground-unit-substitution", RoundPhase::GroundUnitSubstitution, options));
  }
  phases.push_back(
      make_phase_tactic("unit-equality-propagation", RoundPhase::UnitEqualities, options));
  return Tactic("preprocessing-round",
                [phases = std::move(phases)](TacticContext& context, const AnyOf& input) {
                  AnyOf current = input;
                  for (const Tactic& phase : phases) {
                    current = phase(context, current);
                  }
                  return current;
                });
}

}  // namespace primo::smt
