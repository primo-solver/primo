#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace primo::util {

enum class StatKey : std::size_t {
  AssertionsInitial,
  FrontendBooleanItes,
  FrontendAuxiliaryItes,
  AssertionsFinal,
  PreprocessingPasses,
  PreprocessingChangedPasses,
  PreprocessingAdaptivePasses,
  PreprocessingTermSizeInitial,
  PreprocessingTermSizeFinal,
  EqBridgeEqualities,
  EqBridgeSkippedOrNodes,
  BooleanUnitFacts,
  BooleanUnitRewrites,
  BooleanUnitMemoHits,
  BooleanUnitMemoMisses,
  ForcedAtomFacts,
  ForcedAtomDisequalities,
  ForcedAtomRewrites,
  ForcedAtomMemoHits,
  ForcedAtomMemoMisses,
  UnitEqualityConsequences,
  UnitEqualitySubstitutionUnits,
  UnitEqualitySubstitutions,
  UnitEqualitySubstitutionAccepted,
  UnitEqualitySubstitutionRejected,
  SolvedConstantCandidates,
  SolvedConstantAccepted,
  SolvedConstantRewrites,
  SolvedConstantRejectedCycles,
  SolvedConstantRejectedBlowups,
  FiniteDomainDomains,
  FiniteDomainAmoClauses,
  FiniteDomainRawChoices,
  FiniteDomainRawAmoClauses,
  FiniteDomainPostAmoClauses,
  FiniteDomainDedupedAmoClauses,
  FiniteDomainEvaluatedEqualities,
  FiniteDomainEvaluationCutoffs,
  EqualityDefinitions,
  EqualityDefinitionCandidatePairs,
  EqualityDefinitionAcceptedPairs,
  EqualityDefinitionPredictedClauses,
  EqualityDefinitionRejectedDomainSize,
  EqualityDefinitionRejectedClauseLimit,
  GroundCongruenceClauses,
  GroundCongruenceSkippedGroups,
  UfSymmetryAcceptedSets,
  UfSymmetryValues,
  UfSymmetryTerms,
  UfSymmetryConstraints,
  UfSymmetryValuePrecedenceTerms,
  UfSymmetryValuePrecedenceConstraints,
  UfSymmetryRejectedNoninvariant,
  AutoSingleAssertion,
  AutoStrong,
  AutoBasicSingle,
  AutoBasicMulti,
  AutoLargeAssertionSkip,
  SatTheoryNegativePhase,
  SatPhaseAutoPolarity,
  SatPhaseAutoBasicSinglePredicatePolarity,
  SatPhaseAutoBasicMultiPolarity,
  SatVars,
  SatClauses,
  SatClauseLits,
  SatUnitClauses,
  SatBinaryClauses,
  SatConflicts,
  SatDecisions,
  SatPropagations,
  SolverBoolean,
  SolverQfUf,
  SatSolveCalls,
  TheoryAtoms,
  EufPredicateAtoms,
  PureTheoryFilterEnabled,
  PureTheoryAtomsPositive,
  PureTheoryAtomsNegative,
  PureTheoryLiteralsFiltered,
  EufAssignments,
  EufEqAssignments,
  EufDiseqAssignments,
  TheoryChecks,
  TheoryConflicts,
  EufPropagationRounds,
  EufPropagationCandidates,
  EufPropagationDelivered,
  EufPropagationReasonLits,
  EufPropagationBudget,
  EufPropagationDeliveryBudget,
  EufPropagationBudgetCutoffs,
  EufPropagationHitRateCutoffs,
  EufPropagationAutoOnline,
  EufPropagationAutoConflict,
  EufPropagationAutoOff,
  EufIncrementalTerms,
  EufIncrementalFlatDefinitions,
  EufIncrementalMerges,
  EufIncrementalPendingPops,
  EufIncrementalLookupProbes,
  EufIncrementalLookupHits,
  EufIncrementalCongruenceMerges,
  EufIncrementalDisequalityChecks,
  EufIncrementalConflicts,
  EufIncrementalExplainCalls,
  EufIncrementalExplainCacheHits,
  EufIncrementalExplainCacheBoundMisses,
  EufIncrementalExplainLength,
  EufIncrementalExplainTopLevelQueries,
  EufIncrementalExplainRecursiveQueries,
  EufIncrementalExplainRepeatedPairQueries,
  EufIncrementalExplainMaxRecursionDepth,
  EufIncrementalExplainBfsSearches,
  EufIncrementalExplainBfsVisited,
  EufIncrementalExplainBfsPathEdges,
  EufIncrementalExplainBfsMaxQueue,
  EufIncrementalExplainProofForestQueries,
  EufIncrementalExplainProofForestPathEdges,
  EufIncrementalExplainProofForestMaxPathEdges,
  EufIncrementalExplainProofForestMissingLca,
  EufIncrementalExplainProofForestShorterThanBfs,
  EufIncrementalExplainProofForestEqualToBfs,
  EufIncrementalExplainProofForestLongerThanBfs,
  EufIncrementalExplainCausalEdgeSkips,
  EufIncrementalExplainDirectEdges,
  EufIncrementalExplainCongruenceEdges,
  EufIncrementalExplainReasonDuplicateLiterals,
  EufIncrementalConflictReasonMaxLength,
  EufIncrementalConflictReasonLengthLe4,
  EufIncrementalConflictReasonLengthLe8,
  EufIncrementalConflictReasonLengthLe16,
  EufIncrementalConflictReasonLengthLe32,
  EufIncrementalConflictReasonLengthLe64,
  EufIncrementalConflictReasonLengthGt64,
  EufIncrementalBacktracks,
  EufIncrementalTrailEntries,
  EufIncrementalClassMemberTrailEntriesSaved,
  EufIncrementalUseListAppends,
  EufIncrementalUseListDuplicateSkips,
  EufIncrementalAffectedUseListDedup,
  EufIncrementalMaxUseListSize,
  SolverQfLra,
  LraTheoryChecks,
  LraTheoryConflicts,
  LraTheoryPropagations,
  LraBlandFallbacks,
  RealEqualityLowered,
  GuardedRealEqualityDistributed,
  MonotoneEliminationCandidates,
  MonotoneEliminationVariables,
  MonotoneEliminationAtomsFolded,
  SolvedConstantGaussianCandidates,
  LraRowPropagations,
  LraSoiMultiRowConflicts,
  LraSoiQuickxplainCalls,
  LraPivots,
  LraOnlineCheckCalls,
  LraBidirectionalRowPropagations,
  LraTheoryDecisions,
  LraModelPhaseRefreshes,
  LraModelPhaseHints,
  RationalPromotions,
  RationalBigOperations,
  LraTableauVars,
  LraRows,
  LraRowNonzeros,
  LraPivotRowUpdates,
  LraPivotTermOps,
  LraFixedVariables,
  LraEliminationPivots,
  Count,
};

std::string_view stat_name(StatKey key);

class Statistics {
 public:
  using Value = std::uint64_t;

  void set(StatKey key, Value value);
  void set(std::string_view name, Value value);
  void increment(StatKey key, Value delta = 1);
  void increment(std::string_view name, Value delta = 1);

  Value get(StatKey key) const;
  Value get(std::string_view name) const;
  bool contains(StatKey key) const;
  bool contains(std::string_view name) const;
  bool empty() const { return order_.empty() && extra_order_.empty(); }

  std::vector<std::string> keys() const;

 private:
  struct TransparentStringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const noexcept;
    std::size_t operator()(const std::string& value) const noexcept {
      return operator()(std::string_view{value});
    }
  };

  static constexpr std::size_t kStatCount = static_cast<std::size_t>(StatKey::Count);

  static std::optional<StatKey> lookup_key(std::string_view name);
  static std::size_t index(StatKey key) { return static_cast<std::size_t>(key); }

  void remember_key(StatKey key);

  std::array<Value, kStatCount> values_{};
  std::array<bool, kStatCount> seen_{};
  std::vector<StatKey> order_;
  std::vector<std::string> extra_order_;
  std::unordered_map<std::string, Value, TransparentStringHash, std::equal_to<>> extra_values_;
};

void print_statistics(std::ostream& output, const Statistics& statistics);

}  // namespace primo::util
