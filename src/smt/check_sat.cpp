#include "smt/check_sat.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
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
#include "smt/model_builder.hpp"
#include "smt/preprocessing.hpp"
#include "smt/theory_combination.hpp"
#include "smtlib/ast.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/parser.hpp"
#include "theory/arithmetic/linear_polynomial.hpp"
#include "theory/arithmetic/lra_atom.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "theory/equality/congruence_stats.hpp"
#include "theory/equality/explained_congruence_closure.hpp"
#include "theory/equality/flat_congruence_closure.hpp"
#include "theory/equality/flat_terms.hpp"
#include "util/rational.hpp"
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
constexpr util::StatKey kStatRealEqualityLowered = util::StatKey::RealEqualityLowered;
constexpr util::StatKey kStatSolvedConstantGaussianCandidates =
    util::StatKey::SolvedConstantGaussianCandidates;
constexpr util::StatKey kStatFiniteDomainDomains = util::StatKey::FiniteDomainDomains;
constexpr util::StatKey kStatFiniteDomainAmoClauses = util::StatKey::FiniteDomainAmoClauses;
constexpr util::StatKey kStatFiniteDomainRawChoices = util::StatKey::FiniteDomainRawChoices;
constexpr util::StatKey kStatFiniteDomainRawAmoClauses = util::StatKey::FiniteDomainRawAmoClauses;
constexpr util::StatKey kStatFiniteDomainPostAmoClauses = util::StatKey::FiniteDomainPostAmoClauses;
constexpr util::StatKey kStatFiniteDomainDedupedAmoClauses =
    util::StatKey::FiniteDomainDedupedAmoClauses;
constexpr util::StatKey kStatFiniteDomainEvaluatedEqualities =
    util::StatKey::FiniteDomainEvaluatedEqualities;
constexpr util::StatKey kStatFiniteDomainEvaluationCutoffs =
    util::StatKey::FiniteDomainEvaluationCutoffs;
constexpr util::StatKey kStatEqualityDefinitions = util::StatKey::EqualityDefinitions;
constexpr util::StatKey kStatEqualityDefinitionCandidatePairs =
    util::StatKey::EqualityDefinitionCandidatePairs;
constexpr util::StatKey kStatEqualityDefinitionAcceptedPairs =
    util::StatKey::EqualityDefinitionAcceptedPairs;
constexpr util::StatKey kStatEqualityDefinitionPredictedClauses =
    util::StatKey::EqualityDefinitionPredictedClauses;
constexpr util::StatKey kStatEqualityDefinitionRejectedDomainSize =
    util::StatKey::EqualityDefinitionRejectedDomainSize;
constexpr util::StatKey kStatEqualityDefinitionRejectedClauseLimit =
    util::StatKey::EqualityDefinitionRejectedClauseLimit;
constexpr util::StatKey kStatGroundCongruenceClauses = util::StatKey::GroundCongruenceClauses;
constexpr util::StatKey kStatGroundCongruenceSkippedGroups =
    util::StatKey::GroundCongruenceSkippedGroups;
constexpr util::StatKey kStatUfSymmetryAcceptedSets = util::StatKey::UfSymmetryAcceptedSets;
constexpr util::StatKey kStatUfSymmetryValues = util::StatKey::UfSymmetryValues;
constexpr util::StatKey kStatUfSymmetryTerms = util::StatKey::UfSymmetryTerms;
constexpr util::StatKey kStatUfSymmetryConstraints = util::StatKey::UfSymmetryConstraints;
constexpr util::StatKey kStatUfSymmetryValuePrecedenceTerms =
    util::StatKey::UfSymmetryValuePrecedenceTerms;
constexpr util::StatKey kStatUfSymmetryValuePrecedenceConstraints =
    util::StatKey::UfSymmetryValuePrecedenceConstraints;
constexpr util::StatKey kStatUfSymmetryRejectedNoninvariant =
    util::StatKey::UfSymmetryRejectedNoninvariant;
constexpr util::StatKey kStatAutoSingleAssertion = util::StatKey::AutoSingleAssertion;
constexpr util::StatKey kStatAutoStrong = util::StatKey::AutoStrong;
constexpr util::StatKey kStatAutoBasicSingle = util::StatKey::AutoBasicSingle;
constexpr util::StatKey kStatAutoBasicMulti = util::StatKey::AutoBasicMulti;
constexpr util::StatKey kStatAutoLargeAssertionSkip = util::StatKey::AutoLargeAssertionSkip;
constexpr util::StatKey kStatSatTheoryNegativePhase = util::StatKey::SatTheoryNegativePhase;
constexpr util::StatKey kStatSatPhaseAutoPolarity = util::StatKey::SatPhaseAutoPolarity;
constexpr util::StatKey kStatSatPhaseAutoBasicSinglePredicatePolarity =
    util::StatKey::SatPhaseAutoBasicSinglePredicatePolarity;
constexpr util::StatKey kStatSatPhaseAutoBasicMultiPolarity =
    util::StatKey::SatPhaseAutoBasicMultiPolarity;
constexpr util::StatKey kStatSatVars = util::StatKey::SatVars;
constexpr util::StatKey kStatSatClauses = util::StatKey::SatClauses;
constexpr util::StatKey kStatSatClauseLits = util::StatKey::SatClauseLits;
constexpr util::StatKey kStatSatUnitClauses = util::StatKey::SatUnitClauses;
constexpr util::StatKey kStatSatBinaryClauses = util::StatKey::SatBinaryClauses;
constexpr util::StatKey kStatSatConflicts = util::StatKey::SatConflicts;
constexpr util::StatKey kStatSatDecisions = util::StatKey::SatDecisions;
constexpr util::StatKey kStatSatPropagations = util::StatKey::SatPropagations;
constexpr util::StatKey kStatSolverBoolean = util::StatKey::SolverBoolean;
constexpr util::StatKey kStatSolverQfUf = util::StatKey::SolverQfUf;
constexpr util::StatKey kStatSolverQfLra = util::StatKey::SolverQfLra;
constexpr util::StatKey kStatLraTheoryChecks = util::StatKey::LraTheoryChecks;
constexpr util::StatKey kStatLraTheoryConflicts = util::StatKey::LraTheoryConflicts;
constexpr util::StatKey kStatLraTheoryPropagations = util::StatKey::LraTheoryPropagations;
constexpr util::StatKey kStatLraBlandFallbacks = util::StatKey::LraBlandFallbacks;
constexpr util::StatKey kStatLraSoiMultiRowConflicts = util::StatKey::LraSoiMultiRowConflicts;
constexpr util::StatKey kStatLraSoiQuickxplainCalls = util::StatKey::LraSoiQuickxplainCalls;
constexpr util::StatKey kStatLraPivots = util::StatKey::LraPivots;
// Item H3's cost accounting: what the pivots actually cost, not just how
// many there were -- a sparsity rule's whole risk is trading one for the
// other, so both have to be visible.
constexpr util::StatKey kStatLraPivotRowUpdates = util::StatKey::LraPivotRowUpdates;
constexpr util::StatKey kStatLraPivotTermOps = util::StatKey::LraPivotTermOps;
// Distinct from kStatLraTheoryChecks: that one counts iterations of
// solve_qf_lra's outer backend.solve() loop (always 1 under the IPASIR
// online-propagator path, since the whole search happens inside a single
// solve() call) -- not useful for measuring how often the eager
// per-literal tableau_.check() in notify_assignment_lit actually runs.
// This one counts exactly that.
constexpr util::StatKey kStatLraOnlineCheckCalls = util::StatKey::LraOnlineCheckCalls;
constexpr util::StatKey kStatLraRowPropagations = util::StatKey::LraRowPropagations;
constexpr util::StatKey kStatLraBidirectionalRowPropagations =
    util::StatKey::LraBidirectionalRowPropagations;
constexpr util::StatKey kStatLraTheoryDecisions = util::StatKey::LraTheoryDecisions;
constexpr util::StatKey kStatSatSolveCalls = util::StatKey::SatSolveCalls;
constexpr util::StatKey kStatTheoryAtoms = util::StatKey::TheoryAtoms;
constexpr util::StatKey kStatEufPredicateAtoms = util::StatKey::EufPredicateAtoms;
constexpr util::StatKey kStatPureTheoryFilterEnabled = util::StatKey::PureTheoryFilterEnabled;
constexpr util::StatKey kStatPureTheoryAtomsPositive = util::StatKey::PureTheoryAtomsPositive;
constexpr util::StatKey kStatPureTheoryAtomsNegative = util::StatKey::PureTheoryAtomsNegative;
constexpr util::StatKey kStatPureTheoryLiteralsFiltered = util::StatKey::PureTheoryLiteralsFiltered;
constexpr util::StatKey kStatEufAssignments = util::StatKey::EufAssignments;
constexpr util::StatKey kStatEufEqAssignments = util::StatKey::EufEqAssignments;
constexpr util::StatKey kStatEufDiseqAssignments = util::StatKey::EufDiseqAssignments;
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
constexpr std::string_view kStatEufDiseqPropAssignedAppDisequalities =
    ":euf.diseq-prop.assigned-app-disequalities";
constexpr std::string_view kStatEufDiseqPropSourceUnaryCandidates =
    ":euf.diseq-prop.source-unary-candidates";
constexpr std::string_view kStatEufDiseqPropSourceUnaryExistingArgAtom =
    ":euf.diseq-prop.source-unary-existing-arg-atom";
constexpr std::string_view kStatEufDiseqPropSourceUnaryArgAtomTrue =
    ":euf.diseq-prop.source-unary-arg-atom-true";
constexpr std::string_view kStatEufDiseqPropSourceUnaryArgAtomFalse =
    ":euf.diseq-prop.source-unary-arg-atom-false";
constexpr std::string_view kStatEufDiseqPropFlatSameFunctionCandidates =
    ":euf.diseq-prop.flat-same-function-candidates";
constexpr std::string_view kStatEufDiseqPropFlatSameFunctionExistingArgAtom =
    ":euf.diseq-prop.flat-same-function-existing-arg-atom";
constexpr std::string_view kStatEufDiseqPropFlatSameFunctionArgAtomTrue =
    ":euf.diseq-prop.flat-same-function-arg-atom-true";
constexpr std::string_view kStatEufDiseqPropFlatSameFunctionArgAtomFalse =
    ":euf.diseq-prop.flat-same-function-arg-atom-false";
constexpr std::string_view kStatEufDiseqPropGuardedFunctionGuardCandidates =
    ":euf.diseq-prop.guarded-function-guard-candidates";
constexpr std::string_view kStatEufDiseqPropGuardedArgumentGuardCandidates =
    ":euf.diseq-prop.guarded-argument-guard-candidates";
constexpr std::string_view kStatEufAckermannApplications = ":euf.ackermann.applications";
constexpr std::string_view kStatEufAckermannFunctionSymbols = ":euf.ackermann.function-symbols";
constexpr std::string_view kStatEufAckermannEligibleSymbols = ":euf.ackermann.eligible-symbols";
constexpr std::string_view kStatEufAckermannUnarySymbols = ":euf.ackermann.unary-symbols";
constexpr std::string_view kStatEufAckermannNarySymbols = ":euf.ackermann.nary-symbols";
constexpr std::string_view kStatEufAckermannEstimatedConstraints =
    ":euf.ackermann.estimated-constraints";
constexpr std::string_view kStatEufAckermannEstimatedEqualities =
    ":euf.ackermann.estimated-equalities";
constexpr std::string_view kStatEufAckermannEstimatedClauseLits =
    ":euf.ackermann.estimated-clause-lits";
constexpr std::string_view kStatEufAckermannMaxSymbolApplications =
    ":euf.ackermann.max-symbol-applications";
constexpr std::string_view kStatEufAckermannMaxSymbolArity = ":euf.ackermann.max-symbol-arity";
constexpr std::string_view kStatEufAckermannSymbolsCostLe100 = ":euf.ackermann.symbols-cost-le-100";
constexpr std::string_view kStatEufAckermannSymbolsCostLe1000 =
    ":euf.ackermann.symbols-cost-le-1000";
constexpr std::string_view kStatEufAckermannSymbolsCostLe10000 =
    ":euf.ackermann.symbols-cost-le-10000";
constexpr std::string_view kStatEufDynamicAckShadowInvocations =
    ":euf.dynamic-ack.shadow.invocations";
constexpr std::string_view kStatEufDynamicAckShadowResultPairs =
    ":euf.dynamic-ack.shadow.result-pairs";
constexpr std::string_view kStatEufDynamicAckShadowArgAtomsMissing =
    ":euf.dynamic-ack.shadow.arg-atoms-missing";
constexpr std::string_view kStatEufDynamicAckShadowSatVisibleClauses =
    ":euf.dynamic-ack.shadow.sat-visible-clauses";
constexpr std::string_view kStatEufDynamicAckShadowSatisfied = ":euf.dynamic-ack.shadow.satisfied";
constexpr std::string_view kStatEufDynamicAckShadowUnit = ":euf.dynamic-ack.shadow.unit";
constexpr std::string_view kStatEufDynamicAckShadowConflicting =
    ":euf.dynamic-ack.shadow.conflicting";
constexpr std::string_view kStatEufDynamicAckShadowResultDiseqAssigned =
    ":euf.dynamic-ack.shadow.result-diseq-assigned";
constexpr std::string_view kStatEufDynamicAckShadowAllArgsEqAssigned =
    ":euf.dynamic-ack.shadow.all-args-eq-assigned";
constexpr std::string_view kStatEufDynamicAckShadowWidthTotal =
    ":euf.dynamic-ack.shadow.width-total";
constexpr std::string_view kStatEufDynamicAckShadowMaxWidth = ":euf.dynamic-ack.shadow.max-width";
constexpr std::string_view kStatEufDynamicAckShadowWidthLe3 = ":euf.dynamic-ack.shadow.width-le-3";
constexpr std::string_view kStatEufDynamicAckShadowWidthGt3 = ":euf.dynamic-ack.shadow.width-gt-3";
constexpr std::string_view kStatEufDiseqAckCandidates = ":euf.diseq-ack.candidates";
constexpr std::string_view kStatEufDiseqAckQueuePushes = ":euf.diseq-ack.queue-pushes";
constexpr std::string_view kStatEufDiseqAckQueuePops = ":euf.diseq-ack.queue-pops";
constexpr std::string_view kStatEufDiseqAckWidthSkips = ":euf.diseq-ack.width-skips";
constexpr std::string_view kStatEufDiseqAckMissingArgAtom = ":euf.diseq-ack.missing-arg-atom";
constexpr std::string_view kStatEufDiseqAckSatisfiedSkips = ":euf.diseq-ack.satisfied-skips";
constexpr std::string_view kStatEufDiseqAckUnit = ":euf.diseq-ack.unit";
constexpr std::string_view kStatEufDiseqAckConflict = ":euf.diseq-ack.conflict";
constexpr std::string_view kStatEufDiseqAckPropagated = ":euf.diseq-ack.propagated";
constexpr std::string_view kStatEufDiseqAckReturned = ":euf.diseq-ack.returned";
constexpr std::string_view kStatEufDiseqAckPendingCleared = ":euf.diseq-ack.pending-cleared";
constexpr std::string_view kStatEufDiseqAckReasonLits = ":euf.diseq-ack.reason-lits";
constexpr std::string_view kStatEufDiseqAckQueueBudgetCutoffs =
    ":euf.diseq-ack.queue-budget-cutoffs";
constexpr std::string_view kStatEufDiseqAckPerSymbolCutoffs = ":euf.diseq-ack.per-symbol-cutoffs";
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
constexpr util::StatKey kStatEufIncrementalExplainCacheBoundMisses =
    util::StatKey::EufIncrementalExplainCacheBoundMisses;
constexpr util::StatKey kStatEufIncrementalExplainLength =
    util::StatKey::EufIncrementalExplainLength;
constexpr util::StatKey kStatEufIncrementalExplainTopLevelQueries =
    util::StatKey::EufIncrementalExplainTopLevelQueries;
constexpr util::StatKey kStatEufIncrementalExplainRecursiveQueries =
    util::StatKey::EufIncrementalExplainRecursiveQueries;
constexpr util::StatKey kStatEufIncrementalExplainRepeatedPairQueries =
    util::StatKey::EufIncrementalExplainRepeatedPairQueries;
constexpr util::StatKey kStatEufIncrementalExplainMaxRecursionDepth =
    util::StatKey::EufIncrementalExplainMaxRecursionDepth;
constexpr util::StatKey kStatEufIncrementalExplainBfsSearches =
    util::StatKey::EufIncrementalExplainBfsSearches;
constexpr util::StatKey kStatEufIncrementalExplainBfsVisited =
    util::StatKey::EufIncrementalExplainBfsVisited;
constexpr util::StatKey kStatEufIncrementalExplainBfsPathEdges =
    util::StatKey::EufIncrementalExplainBfsPathEdges;
constexpr util::StatKey kStatEufIncrementalExplainBfsMaxQueue =
    util::StatKey::EufIncrementalExplainBfsMaxQueue;
constexpr util::StatKey kStatEufIncrementalExplainProofForestQueries =
    util::StatKey::EufIncrementalExplainProofForestQueries;
constexpr util::StatKey kStatEufIncrementalExplainProofForestPathEdges =
    util::StatKey::EufIncrementalExplainProofForestPathEdges;
constexpr util::StatKey kStatEufIncrementalExplainProofForestMaxPathEdges =
    util::StatKey::EufIncrementalExplainProofForestMaxPathEdges;
constexpr util::StatKey kStatEufIncrementalExplainProofForestMissingLca =
    util::StatKey::EufIncrementalExplainProofForestMissingLca;
constexpr util::StatKey kStatEufIncrementalExplainProofForestShorterThanBfs =
    util::StatKey::EufIncrementalExplainProofForestShorterThanBfs;
constexpr util::StatKey kStatEufIncrementalExplainProofForestEqualToBfs =
    util::StatKey::EufIncrementalExplainProofForestEqualToBfs;
constexpr util::StatKey kStatEufIncrementalExplainProofForestLongerThanBfs =
    util::StatKey::EufIncrementalExplainProofForestLongerThanBfs;
constexpr util::StatKey kStatEufIncrementalExplainCausalEdgeSkips =
    util::StatKey::EufIncrementalExplainCausalEdgeSkips;
constexpr util::StatKey kStatEufIncrementalExplainDirectEdges =
    util::StatKey::EufIncrementalExplainDirectEdges;
constexpr util::StatKey kStatEufIncrementalExplainCongruenceEdges =
    util::StatKey::EufIncrementalExplainCongruenceEdges;
constexpr util::StatKey kStatEufIncrementalExplainReasonDuplicateLiterals =
    util::StatKey::EufIncrementalExplainReasonDuplicateLiterals;
constexpr util::StatKey kStatEufIncrementalConflictReasonMaxLength =
    util::StatKey::EufIncrementalConflictReasonMaxLength;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe4 =
    util::StatKey::EufIncrementalConflictReasonLengthLe4;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe8 =
    util::StatKey::EufIncrementalConflictReasonLengthLe8;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe16 =
    util::StatKey::EufIncrementalConflictReasonLengthLe16;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe32 =
    util::StatKey::EufIncrementalConflictReasonLengthLe32;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthLe64 =
    util::StatKey::EufIncrementalConflictReasonLengthLe64;
constexpr util::StatKey kStatEufIncrementalConflictReasonLengthGt64 =
    util::StatKey::EufIncrementalConflictReasonLengthGt64;
constexpr util::StatKey kStatEufIncrementalBacktracks = util::StatKey::EufIncrementalBacktracks;
constexpr util::StatKey kStatEufIncrementalTrailEntries = util::StatKey::EufIncrementalTrailEntries;
constexpr util::StatKey kStatEufIncrementalClassMemberTrailEntriesSaved =
    util::StatKey::EufIncrementalClassMemberTrailEntriesSaved;
constexpr util::StatKey kStatEufIncrementalUseListAppends =
    util::StatKey::EufIncrementalUseListAppends;
constexpr util::StatKey kStatEufIncrementalUseListDuplicateSkips =
    util::StatKey::EufIncrementalUseListDuplicateSkips;
constexpr util::StatKey kStatEufIncrementalAffectedUseListDedup =
    util::StatKey::EufIncrementalAffectedUseListDedup;
constexpr util::StatKey kStatEufIncrementalMaxUseListSize =
    util::StatKey::EufIncrementalMaxUseListSize;
constexpr std::string_view kStatTimeTotalUs = ":time.total-us";
constexpr std::string_view kStatTimeParseUs = ":time.parse-us";
constexpr std::string_view kStatTimeFrontendUs = ":time.frontend-us";
constexpr std::string_view kStatTimePreprocessingUs = ":time.preprocessing-us";
constexpr std::string_view kStatTimeSolveUs = ":time.solve-us";

theory::equality::FlatExplanationSearchMode flat_explanation_search_mode(
    EufExplainSearchMode mode) {
  switch (mode) {
    case EufExplainSearchMode::BidirectionalBfs:
      return theory::equality::FlatExplanationSearchMode::BidirectionalBfs;
    case EufExplainSearchMode::ProofForest:
      return theory::equality::FlatExplanationSearchMode::ProofForest;
    case EufExplainSearchMode::ReorientedProofForest:
      return theory::equality::FlatExplanationSearchMode::ReorientedProofForest;
    case EufExplainSearchMode::StructuralProofForest:
      return theory::equality::FlatExplanationSearchMode::StructuralProofForest;
    case EufExplainSearchMode::OneSidedBfs:
      return theory::equality::FlatExplanationSearchMode::OneSidedBfs;
  }
  return theory::equality::FlatExplanationSearchMode::OneSidedBfs;
}

theory::arithmetic::LraTableau::PivotingRule lra_pivoting_rule_from_options(LraPivotingRule rule) {
  switch (rule) {
    case LraPivotingRule::Dantzig:
      return theory::arithmetic::LraTableau::PivotingRule::Dantzig;
    case LraPivotingRule::Bland:
      return theory::arithmetic::LraTableau::PivotingRule::Bland;
    case LraPivotingRule::Soi:
      return theory::arithmetic::LraTableau::PivotingRule::Soi;
    case LraPivotingRule::Sparse:
      return theory::arithmetic::LraTableau::PivotingRule::Sparse;
  }
  return theory::arithmetic::LraTableau::PivotingRule::Bland;
}

constexpr std::size_t kAutoPreprocessLargeAssertionLimit = 10000;
constexpr std::size_t kAutoBasicMultiTermLimit = 20000;
constexpr std::size_t kAutoBasicSingleTermLimit = 15000;
constexpr std::size_t kAutoBasicSingleMinimumDomains = 200;
constexpr std::size_t kAutoBasicSingleMaximumSymmetryTerms = 6;
constexpr std::size_t kLiveStatsFlushInterval = 1024;
using PhaseClock = std::chrono::steady_clock;

util::Statistics::Value elapsed_microseconds(PhaseClock::time_point start,
                                             PhaseClock::time_point end) {
  return static_cast<util::Statistics::Value>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

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

void record_ackermann_cost_diagnostics(const core::TermManager& terms,
                                       const std::vector<core::TermId>& roots,
                                       util::Statistics& statistics) {
  std::vector<unsigned char> seen(terms.size(), 0);
  std::vector<core::TermId> work = roots;
  std::unordered_map<core::SymbolId, std::vector<core::TermId>> applications_by_symbol;
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
    const core::Term& term = terms.get(term_id);
    if (term.kind == core::TermKind::Application) {
      applications_by_symbol[term.symbol].push_back(term_id);
    }
    work.insert(work.end(), term.children.begin(), term.children.end());
  }

  std::size_t applications = 0;
  std::size_t eligible_symbols = 0;
  std::size_t unary_symbols = 0;
  std::size_t nary_symbols = 0;
  std::size_t estimated_constraints = 0;
  std::size_t estimated_equalities = 0;
  std::size_t max_symbol_applications = 0;
  std::size_t max_symbol_arity = 0;
  std::size_t symbols_cost_le_100 = 0;
  std::size_t symbols_cost_le_1000 = 0;
  std::size_t symbols_cost_le_10000 = 0;

  for (const auto& [symbol_id, symbol_applications] : applications_by_symbol) {
    const core::Symbol& symbol = terms.symbols().get_symbol(symbol_id);
    const std::size_t arity = symbol.arg_sorts.size();
    const std::size_t count = symbol_applications.size();
    applications += count;
    max_symbol_applications = std::max(max_symbol_applications, count);
    max_symbol_arity = std::max(max_symbol_arity, arity);
    if (arity == 1) {
      ++unary_symbols;
    } else if (arity > 1) {
      ++nary_symbols;
    }
    if (count < 2) {
      continue;
    }
    ++eligible_symbols;
    const std::size_t pairs = count * (count - 1U) / 2U;
    if (pairs <= 100) {
      ++symbols_cost_le_100;
    }
    if (pairs <= 1000) {
      ++symbols_cost_le_1000;
    }
    if (pairs <= 10000) {
      ++symbols_cost_le_10000;
    }
    estimated_constraints += pairs;
    estimated_equalities += pairs * (arity + 1U);
  }

  statistics.set(kStatEufAckermannApplications, applications);
  statistics.set(kStatEufAckermannFunctionSymbols, applications_by_symbol.size());
  statistics.set(kStatEufAckermannEligibleSymbols, eligible_symbols);
  statistics.set(kStatEufAckermannUnarySymbols, unary_symbols);
  statistics.set(kStatEufAckermannNarySymbols, nary_symbols);
  statistics.set(kStatEufAckermannEstimatedConstraints, estimated_constraints);
  statistics.set(kStatEufAckermannEstimatedEqualities, estimated_equalities);
  statistics.set(kStatEufAckermannEstimatedClauseLits, estimated_equalities);
  statistics.set(kStatEufAckermannMaxSymbolApplications, max_symbol_applications);
  statistics.set(kStatEufAckermannMaxSymbolArity, max_symbol_arity);
  statistics.set(kStatEufAckermannSymbolsCostLe100, symbols_cost_le_100);
  statistics.set(kStatEufAckermannSymbolsCostLe1000, symbols_cost_le_1000);
  statistics.set(kStatEufAckermannSymbolsCostLe10000, symbols_cost_le_10000);
}

void publish_sat_stats(const sat::BackendStatistics& sat_stats, util::Statistics& statistics) {
  statistics.set(kStatSatVars, sat_stats.variables);
  statistics.set(kStatSatClauses, sat_stats.clauses);
  statistics.set(kStatSatClauseLits, sat_stats.clause_literals);
  statistics.set(kStatSatUnitClauses, sat_stats.unit_clauses);
  statistics.set(kStatSatBinaryClauses, sat_stats.binary_clauses);
  statistics.set(kStatSatConflicts, sat_stats.conflicts);
  statistics.set(kStatSatDecisions, sat_stats.decisions);
  statistics.set(kStatSatPropagations, sat_stats.propagations);
}

// See check_sat.hpp's refresh_active_solve_stats() comment for why this
// exists: CaDiCaL's own counters are live at any time, but check_sat only
// copies them into the caller-visible Statistics synchronously around each
// blocking solve() call, so a signal handler firing mid-solve otherwise
// sees stale (often zero) values. `backend`/`statistics` here point at the
// call's own locals for the duration of the enclosing solve function only;
// the guard's destructor clears them on every exit path (normal return or
// stack unwinding), so there is never a dangling pointer for the handler to
// read from outside that scope.
const sat::IpasirUpBackend* g_active_backend = nullptr;
util::Statistics* g_active_backend_statistics = nullptr;

class ActiveSolveStatsGuard {
 public:
  ActiveSolveStatsGuard(const sat::IpasirUpBackend& backend, util::Statistics& statistics) {
    g_active_backend = &backend;
    g_active_backend_statistics = &statistics;
  }
  ~ActiveSolveStatsGuard() {
    g_active_backend = nullptr;
    g_active_backend_statistics = nullptr;
  }
  ActiveSolveStatsGuard(const ActiveSolveStatsGuard&) = delete;
  ActiveSolveStatsGuard& operator=(const ActiveSolveStatsGuard&) = delete;
};

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
  constexpr std::size_t kAutoOnlineFiniteDomainDomainLimit = 500;
  const std::size_t finite_domain_domains = statistics.get(kStatFiniteDomainDomains);
  const std::size_t pure_negative_atoms = statistics.get(kStatPureTheoryAtomsNegative);
  if (finite_domain_domains >= kAutoOnlineFiniteDomainDomainLimit &&
      pure_negative_atoms <= finite_domain_domains) {
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

class Frontend;

// Builds a core::TermId tree for one top-level Frontend::build_term(root)
// call, iteratively -- deliberately not via recursive descent, since
// fuzzer-generated input can nest arbitrarily deep (long `let`/arithmetic
// chains in particular) and a native-recursive version can stack-overflow
// (found via fuzzing under ASAN, see docs/run-log.json). Every place the
// term-building logic used to call build_term(child) and use its result
// becomes: schedule(child, continuation) -- the continuation is a plain
// heap-allocated closure capturing whatever local state that call site
// needed to resume with, pushed onto `pending_` instead of onto the
// native call stack. run()'s driver loop repeatedly pops and invokes one
// pending closure until the root's result is available; no C++ call
// recursion happens regardless of input nesting depth. A fresh
// TermBuilder is constructed per build_term(root) call (see
// Frontend::build_term below) -- it holds no state that needs to survive
// across separate assertions.
//
// A Frontend::Definition's body may itself invoke another definition (or,
// guarded by expanding_definitions_, itself), so definition instantiation
// is folded into this same driver rather than calling Frontend::build_term
// again -- a second, nested synchronous build_term call from within a
// continuation would just reintroduce native recursion one level higher
// up (a chain of nested definition *calls* in the source, not just nested
// definition *bodies*).
class TermBuilder {
 public:
  explicit TermBuilder(Frontend& frontend) : frontend_(frontend) {}

  core::TermId run(const smtlib::SExpr& root);

 private:
  using Continuation = std::function<void(core::TermId)>;

  // Arranges for `expr` to be evaluated and `k` invoked with the result,
  // without recursing (pushes a closure onto `pending_`; the driver loop
  // in run() calls it later).
  void schedule(const smtlib::SExpr& expr, Continuation k);

  // Invokes continuation `k` with `value` -- via `pending_`, never
  // directly. This is the other half of making the whole builder
  // non-recursive: `schedule` alone only defers *descending* into a
  // child's evaluation, but a continuation's body routinely calls
  // *another* continuation synchronously once it has a combined result
  // (e.g. dispatch_plus's "all children ready" callback calling k with
  // mk_plus(...)) -- chained deeply enough (e.g. a long `+`/`let` chain),
  // those direct calls alone reconstruct exactly the same native
  // recursion this class exists to avoid. Routing every delivery through
  // `pending_` here keeps each individual call O(1) stack depth
  // regardless of how many levels of nesting ultimately resolve.
  void resume(Continuation k, core::TermId value);

  // Evaluates every expression in `exprs`, in order, then invokes
  // `done` with all their TermIds (same order) -- the iterative
  // replacement for `for (child : children) results.push_back(build_term(child));`.
  void schedule_all(std::shared_ptr<std::vector<const smtlib::SExpr*>> exprs,
                    std::function<void(std::vector<core::TermId>)> done);
  void schedule_all_from(std::shared_ptr<std::vector<const smtlib::SExpr*>> exprs,
                         std::size_t index, std::vector<core::TermId> results,
                         std::function<void(std::vector<core::TermId>)> done);

  static std::shared_ptr<std::vector<const smtlib::SExpr*>> collect(const smtlib::SExpr& expr,
                                                                    std::size_t start);

  void dispatch(const smtlib::SExpr& expr, Continuation k);
  void dispatch_let(const smtlib::SExpr& expr, Continuation k);
  void dispatch_ite(const smtlib::SExpr& expr, Continuation k);
  void finish_ite(core::TermId condition, core::TermId then_term, core::TermId else_term,
                  Continuation k);
  void dispatch_implies(const smtlib::SExpr& expr, Continuation k);
  void dispatch_xor(const smtlib::SExpr& expr, Continuation k);
  void dispatch_equality(const smtlib::SExpr& expr, Continuation k);
  core::TermId finish_pairwise_equality_to_ite(core::TermId term, core::TermId condition,
                                               core::TermId then_term, core::TermId else_term);
  void dispatch_distinct(const smtlib::SExpr& expr, Continuation k);
  void dispatch_plus(const smtlib::SExpr& expr, Continuation k);
  void dispatch_minus(const smtlib::SExpr& expr, Continuation k);
  void dispatch_times(const smtlib::SExpr& expr, Continuation k);
  void dispatch_divide(const smtlib::SExpr& expr, Continuation k);
  void dispatch_comparison(const smtlib::SExpr& expr, const std::string& head, Continuation k);
  void dispatch_instantiate_definition(const std::string& name, std::vector<core::TermId> args,
                                       Continuation k);

  Frontend& frontend_;
  std::vector<std::function<void()>> pending_;
  std::optional<core::TermId> result_;
};

class Frontend {
 public:
  friend class TermBuilder;

  core::TermId build_term(const smtlib::SExpr& expr) {
    TermBuilder builder(*this);
    return builder.run(expr);
  }

  void process_command(const smtlib::Command& command, std::vector<core::TermId>& assertions) {
    switch (command.kind) {
      case smtlib::CommandKind::Ignored:
        return;
      case smtlib::CommandKind::SetOption:
        if (command.option == ":produce-models") {
          if (command.value == "true") {
            produce_models_ = true;
          } else if (command.value == "false") {
            produce_models_ = false;
          } else {
            throw UnsupportedInput(":produce-models expects true or false");
          }
        }
        return;
      case smtlib::CommandKind::SetLogic:
        if (command.logic != "QF_UF" && command.logic != "QF_LRA" && command.logic != "QF_UFLRA") {
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
      case smtlib::CommandKind::GetModel:
        if (!saw_check_sat_) {
          throw UnsupportedInput("get-model before check-sat is unsupported");
        }
        requested_model_ = true;
        return;
    }
  }

  bool saw_check_sat() const { return saw_check_sat_; }
  bool requested_model() const { return requested_model_; }
  bool produce_models() const { return produce_models_; }
  const std::vector<core::TermId>& declared_real_constants() const {
    return declared_real_constants_;
  }
  const core::SymbolTable& symbols() const { return symbols_; }
  const core::TermManager& terms() const { return terms_; }
  core::TermManager& terms() { return terms_; }
  std::size_t boolean_ites() const { return boolean_ites_; }
  std::size_t auxiliary_ites() const { return auxiliary_ites_; }
  core::TermId fresh_internal_constant(core::SortId sort, std::string_view prefix) {
    return fresh_auxiliary_constant(sort, prefix);
  }

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
      const core::SymbolId symbol = symbols_.declare_constant(command.name, result_sort);
      if (result_sort == symbols_.real_sort()) {
        declared_real_constants_.push_back(terms_.mk_constant(symbol));
      }
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

  // Definition instantiation's actual body-building (which used to be
  // here, calling build_term(definition.body) recursively) now lives in
  // TermBuilder::dispatch_instantiate_definition, folded into the same
  // iterative driver as everything else -- see the TermBuilder class
  // comment above Frontend for why a second nested build_term call
  // wouldn't have actually fixed the recursion.

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
  std::vector<core::TermId> declared_real_constants_;
  std::size_t auxiliary_counter_ = 0;
  std::size_t boolean_ites_ = 0;
  std::size_t auxiliary_ites_ = 0;
  bool saw_check_sat_ = false;
  bool requested_model_ = false;
  bool produce_models_ = false;
};

core::TermId TermBuilder::run(const smtlib::SExpr& root) {
  schedule(root, [this](core::TermId id) { result_ = id; });
  while (!result_.has_value()) {
    std::function<void()> job = std::move(pending_.back());
    pending_.pop_back();
    job();
  }
  return result_.value();
}

void TermBuilder::schedule(const smtlib::SExpr& expr, Continuation k) {
  pending_.push_back([this, &expr, k = std::move(k)]() mutable { dispatch(expr, std::move(k)); });
}

void TermBuilder::resume(Continuation k, core::TermId value) {
  pending_.push_back([k = std::move(k), value]() { k(value); });
}

void TermBuilder::schedule_all(std::shared_ptr<std::vector<const smtlib::SExpr*>> exprs,
                               std::function<void(std::vector<core::TermId>)> done) {
  schedule_all_from(std::move(exprs), 0, {}, std::move(done));
}

void TermBuilder::schedule_all_from(std::shared_ptr<std::vector<const smtlib::SExpr*>> exprs,
                                    std::size_t index, std::vector<core::TermId> results,
                                    std::function<void(std::vector<core::TermId>)> done) {
  if (index == exprs->size()) {
    done(std::move(results));
    return;
  }
  schedule(*(*exprs)[index], [this, exprs, index, results = std::move(results),
                              done = std::move(done)](core::TermId r) mutable {
    results.push_back(r);
    schedule_all_from(std::move(exprs), index + 1, std::move(results), std::move(done));
  });
}

std::shared_ptr<std::vector<const smtlib::SExpr*>> TermBuilder::collect(const smtlib::SExpr& expr,
                                                                        std::size_t start) {
  auto exprs = std::make_shared<std::vector<const smtlib::SExpr*>>();
  exprs->reserve(expr.list.size() - start);
  for (std::size_t i = start; i < expr.list.size(); ++i) {
    exprs->push_back(&expr.list[i]);
  }
  return exprs;
}

void TermBuilder::dispatch(const smtlib::SExpr& expr, Continuation k) {
  if (expr.kind == smtlib::SExpr::Kind::Atom) {
    if (expr.atom == "true") {
      resume(std::move(k), frontend_.terms_.mk_bool(true));
      return;
    }
    if (expr.atom == "false") {
      resume(std::move(k), frontend_.terms_.mk_bool(false));
      return;
    }
    if (const auto literal = util::Rational::parse(expr.atom); literal.has_value()) {
      resume(std::move(k), frontend_.terms_.mk_rational(literal.value()));
      return;
    }
    const auto bound = frontend_.lookup_bound_term(expr.atom);
    if (bound.has_value()) {
      resume(std::move(k), bound.value());
      return;
    }
    if (!frontend_.definitions_.empty()) {
      const auto defined = frontend_.definitions_.find(expr.atom);
      if (defined != frontend_.definitions_.end() && defined->second.parameters.empty()) {
        dispatch_instantiate_definition(expr.atom, {}, std::move(k));
        return;
      }
    }
    const auto symbol = frontend_.symbols_.lookup_symbol(expr.atom);
    if (!symbol.has_value()) {
      throw UnsupportedInput("unknown symbol: " + expr.atom);
    }
    resume(std::move(k), frontend_.terms_.mk_constant(symbol.value()));
    return;
  }

  if (expr.list.empty() || expr.list[0].kind != smtlib::SExpr::Kind::Atom) {
    throw UnsupportedInput("unsupported expression shape");
  }

  const std::string& head = expr.list[0].atom;
  if (head == "not") {
    Frontend::require_arity(expr, 2, "not");
    schedule(expr.list[1], [this, k = std::move(k)](core::TermId x) mutable {
      resume(std::move(k), frontend_.terms_.mk_not(x));
    });
    return;
  }
  if (head == "and") {
    schedule_all(collect(expr, 1),
                 [this, k = std::move(k)](std::vector<core::TermId> children) mutable {
                   resume(std::move(k), frontend_.terms_.mk_and(std::move(children)));
                 });
    return;
  }
  if (head == "or") {
    schedule_all(collect(expr, 1),
                 [this, k = std::move(k)](std::vector<core::TermId> children) mutable {
                   resume(std::move(k), frontend_.terms_.mk_or(std::move(children)));
                 });
    return;
  }
  if (head == "=>") {
    dispatch_implies(expr, std::move(k));
    return;
  }
  if (head == "xor") {
    dispatch_xor(expr, std::move(k));
    return;
  }
  if (head == "!") {
    if (expr.list.size() < 2) {
      throw UnsupportedInput("annotated term has no body");
    }
    schedule(expr.list[1], std::move(k));
    return;
  }
  if (head == "ite") {
    dispatch_ite(expr, std::move(k));
    return;
  }
  if (head == "let") {
    dispatch_let(expr, std::move(k));
    return;
  }
  if (head == "=") {
    dispatch_equality(expr, std::move(k));
    return;
  }
  if (head == "distinct") {
    dispatch_distinct(expr, std::move(k));
    return;
  }
  if (head == "+") {
    dispatch_plus(expr, std::move(k));
    return;
  }
  if (head == "-") {
    dispatch_minus(expr, std::move(k));
    return;
  }
  if (head == "*") {
    dispatch_times(expr, std::move(k));
    return;
  }
  if (head == "/") {
    dispatch_divide(expr, std::move(k));
    return;
  }
  if (head == "<=" || head == "<" || head == ">=" || head == ">") {
    dispatch_comparison(expr, head, std::move(k));
    return;
  }

  if (!frontend_.definitions_.empty()) {
    const auto defined = frontend_.definitions_.find(head);
    if (defined != frontend_.definitions_.end()) {
      schedule_all(collect(expr, 1),
                   [this, k = std::move(k), head](std::vector<core::TermId> args) mutable {
                     dispatch_instantiate_definition(head, std::move(args), k);
                   });
      return;
    }
  }

  const auto symbol = frontend_.symbols_.lookup_symbol(head);
  if (!symbol.has_value()) {
    throw UnsupportedInput("unknown function symbol: " + head);
  }
  schedule_all(collect(expr, 1), [this, k = std::move(k), symbol = symbol.value()](
                                     std::vector<core::TermId> raw_args) mutable {
    std::vector<core::TermId> args;
    args.reserve(raw_args.size());
    for (const core::TermId a : raw_args) {
      args.push_back(frontend_.bind_bool_argument_if_needed(a));
    }
    resume(std::move(k), frontend_.terms_.mk_app(symbol, std::move(args)));
  });
}

void TermBuilder::dispatch_let(const smtlib::SExpr& expr, Continuation k) {
  Frontend::require_arity(expr, 3, "let");
  if (expr.list[1].kind != smtlib::SExpr::Kind::List) {
    throw UnsupportedInput("let bindings must be a list");
  }
  const smtlib::SExpr& bindings_expr = expr.list[1];
  for (const smtlib::SExpr& binding : bindings_expr.list) {
    if (binding.kind != smtlib::SExpr::Kind::List || binding.list.size() != 2 ||
        binding.list[0].kind != smtlib::SExpr::Kind::Atom) {
      throw UnsupportedInput("malformed let binding");
    }
  }
  auto binding_value_exprs = std::make_shared<std::vector<const smtlib::SExpr*>>();
  binding_value_exprs->reserve(bindings_expr.list.size());
  for (const smtlib::SExpr& binding : bindings_expr.list) {
    binding_value_exprs->push_back(&binding.list[1]);
  }
  schedule_all(binding_value_exprs, [this, &expr, &bindings_expr,
                                     k = std::move(k)](std::vector<core::TermId> values) mutable {
    std::unordered_map<std::string, core::TermId> scope;
    for (std::size_t i = 0; i < bindings_expr.list.size(); ++i) {
      const std::string& name = bindings_expr.list[i].list[0].atom;
      if (!scope.emplace(name, values[i]).second) {
        throw UnsupportedInput("duplicate let binding: " + name);
      }
    }
    frontend_.let_scopes_.push_back(std::move(scope));
    schedule(expr.list[2], [this, k = std::move(k)](core::TermId body) mutable {
      frontend_.let_scopes_.pop_back();
      resume(std::move(k), body);
    });
  });
}

void TermBuilder::dispatch_ite(const smtlib::SExpr& expr, Continuation k) {
  Frontend::require_arity(expr, 4, "ite");
  schedule(expr.list[1], [this, &expr, k = std::move(k)](core::TermId condition) mutable {
    if (frontend_.terms_.get(condition).sort != frontend_.symbols_.bool_sort()) {
      throw UnsupportedInput("ite condition is not Boolean");
    }
    auto branches = std::make_shared<std::vector<const smtlib::SExpr*>>(
        std::vector<const smtlib::SExpr*>{&expr.list[2], &expr.list[3]});
    schedule_all(branches, [this, condition,
                            k = std::move(k)](std::vector<core::TermId> branch_terms) mutable {
      finish_ite(condition, branch_terms[0], branch_terms[1], std::move(k));
    });
  });
}

void TermBuilder::finish_ite(core::TermId condition, core::TermId then_term, core::TermId else_term,
                             Continuation k) {
  const core::SortId result_sort = frontend_.terms_.get(then_term).sort;
  if (result_sort != frontend_.terms_.get(else_term).sort) {
    throw UnsupportedInput("ite branch sort mismatch");
  }
  if (result_sort == frontend_.symbols_.bool_sort()) {
    ++frontend_.boolean_ites_;
    resume(std::move(k),
           frontend_.terms_.mk_or(
               {frontend_.terms_.mk_and({condition, then_term}),
                frontend_.terms_.mk_and({frontend_.terms_.mk_not(condition), else_term})}));
    return;
  }
  const core::TermId auxiliary = frontend_.fresh_auxiliary_constant(result_sort, "ite");
  ++frontend_.auxiliary_ites_;
  frontend_.side_assertions_.push_back(
      frontend_.terms_.mk_implies(condition, frontend_.terms_.mk_eq(auxiliary, then_term)));
  frontend_.side_assertions_.push_back(frontend_.terms_.mk_implies(
      frontend_.terms_.mk_not(condition), frontend_.terms_.mk_eq(auxiliary, else_term)));
  resume(std::move(k), auxiliary);
}

void TermBuilder::dispatch_implies(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() < 3) {
    throw UnsupportedInput("=> has wrong arity");
  }
  // Matches the original right-fold's evaluation order: last operand
  // first, then descending down to the first.
  auto exprs = std::make_shared<std::vector<const smtlib::SExpr*>>();
  for (std::size_t idx = expr.list.size() - 1; idx >= 1; --idx) {
    exprs->push_back(&expr.list[idx]);
    if (idx == 1) {
      break;
    }
  }
  schedule_all(exprs, [this, k = std::move(k)](std::vector<core::TermId> reversed_results) mutable {
    core::TermId result = reversed_results[0];
    for (std::size_t j = 1; j < reversed_results.size(); ++j) {
      result = frontend_.terms_.mk_implies(reversed_results[j], result);
    }
    resume(std::move(k), result);
  });
}

void TermBuilder::dispatch_xor(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() < 3) {
    throw UnsupportedInput("xor has wrong arity");
  }
  schedule_all(collect(expr, 1), [this,
                                  k = std::move(k)](std::vector<core::TermId> operands) mutable {
    core::TermId result = operands[0];
    for (std::size_t i = 1; i < operands.size(); ++i) {
      const core::TermId rhs = operands[i];
      result =
          frontend_.terms_.mk_or({frontend_.terms_.mk_and({result, frontend_.terms_.mk_not(rhs)}),
                                  frontend_.terms_.mk_and({frontend_.terms_.mk_not(result), rhs})});
    }
    resume(std::move(k), result);
  });
}

void TermBuilder::dispatch_equality(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() < 3) {
    throw UnsupportedInput("= has wrong arity");
  }

  // Mirrors build_equality's original sequential loop exactly, but
  // instead of calling build_term/build_pairwise_equality_to_ite directly
  // (which would recurse), it walks the SAME control flow purely
  // symbolically -- the branching depends only on has_head() checks on
  // the already-fully-parsed SExpr shape, never on any evaluated term, so
  // it's safe to precompute the whole plan upfront -- recording which
  // atomic sub-expressions need evaluating (as indices into one flat job
  // list) and a parallel step plan to replay, non-recursively, once every
  // job's TermId is available.
  struct Step {
    bool current_is_ite = false;
    bool reuse_previous = false;
    std::size_t lhs_index = 0;
    std::size_t ite_cond_index = 0, ite_then_index = 0, ite_else_index = 0;
    std::size_t current_index = 0;
    bool previous_is_ite = false;
    std::size_t prev_ite_cond_index = 0, prev_ite_then_index = 0, prev_ite_else_index = 0;
  };

  auto flat_exprs = std::make_shared<std::vector<const smtlib::SExpr*>>();
  auto push_expr = [&flat_exprs](const smtlib::SExpr& e) {
    flat_exprs->push_back(&e);
    return flat_exprs->size() - 1;
  };
  auto steps = std::make_shared<std::vector<Step>>();

  const smtlib::SExpr* previous_expr = &expr.list[1];
  bool previous_term_valid = false;

  for (std::size_t i = 2; i < expr.list.size(); ++i) {
    const smtlib::SExpr& current_expr = expr.list[i];
    Step step;
    if (Frontend::has_head(current_expr, "ite")) {
      Frontend::require_arity(current_expr, 4, "ite");
      step.current_is_ite = true;
      step.reuse_previous = previous_term_valid;
      if (!previous_term_valid) {
        step.lhs_index = push_expr(*previous_expr);
      }
      step.ite_cond_index = push_expr(current_expr.list[1]);
      step.ite_then_index = push_expr(current_expr.list[2]);
      step.ite_else_index = push_expr(current_expr.list[3]);
      steps->push_back(step);
      previous_expr = &current_expr;
      previous_term_valid = false;
      continue;
    }
    step.current_is_ite = false;
    step.current_index = push_expr(current_expr);
    if (Frontend::has_head(*previous_expr, "ite")) {
      step.previous_is_ite = true;
      Frontend::require_arity(*previous_expr, 4, "ite");
      step.prev_ite_cond_index = push_expr(previous_expr->list[1]);
      step.prev_ite_then_index = push_expr(previous_expr->list[2]);
      step.prev_ite_else_index = push_expr(previous_expr->list[3]);
    } else {
      step.previous_is_ite = false;
      step.reuse_previous = previous_term_valid;
      if (!previous_term_valid) {
        step.lhs_index = push_expr(*previous_expr);
      }
    }
    steps->push_back(step);
    previous_expr = &current_expr;
    previous_term_valid = true;
  }

  schedule_all(
      flat_exprs, [this, k = std::move(k), steps](std::vector<core::TermId> results) mutable {
        std::vector<core::TermId> equalities;
        equalities.reserve(steps->size());
        std::optional<core::TermId> previous_term;
        for (const Step& step : *steps) {
          if (step.current_is_ite) {
            const core::TermId lhs =
                step.reuse_previous ? previous_term.value() : results[step.lhs_index];
            equalities.push_back(finish_pairwise_equality_to_ite(lhs, results[step.ite_cond_index],
                                                                 results[step.ite_then_index],
                                                                 results[step.ite_else_index]));
            previous_term.reset();
            continue;
          }
          const core::TermId current = results[step.current_index];
          if (step.previous_is_ite) {
            equalities.push_back(finish_pairwise_equality_to_ite(
                current, results[step.prev_ite_cond_index], results[step.prev_ite_then_index],
                results[step.prev_ite_else_index]));
          } else {
            const core::TermId lhs =
                step.reuse_previous ? previous_term.value() : results[step.lhs_index];
            equalities.push_back(frontend_.build_pairwise_equality(lhs, current));
          }
          previous_term = current;
        }
        resume(std::move(k), frontend_.terms_.mk_and(std::move(equalities)));
      });
}

core::TermId TermBuilder::finish_pairwise_equality_to_ite(core::TermId term, core::TermId condition,
                                                          core::TermId then_term,
                                                          core::TermId else_term) {
  if (frontend_.terms_.get(condition).sort != frontend_.symbols_.bool_sort()) {
    throw UnsupportedInput("ite condition is not Boolean");
  }
  const core::SortId result_sort = frontend_.terms_.get(then_term).sort;
  if (result_sort != frontend_.terms_.get(else_term).sort) {
    throw UnsupportedInput("ite branch sort mismatch");
  }
  if (frontend_.terms_.get(term).sort != result_sort) {
    throw UnsupportedInput("equality sort mismatch");
  }
  return frontend_.terms_.mk_and(
      {frontend_.terms_.mk_implies(condition, frontend_.build_pairwise_equality(term, then_term)),
       frontend_.terms_.mk_implies(frontend_.terms_.mk_not(condition),
                                   frontend_.build_pairwise_equality(term, else_term))});
}

void TermBuilder::dispatch_distinct(const smtlib::SExpr& expr, Continuation k) {
  schedule_all(collect(expr, 1), [this, k = std::move(k)](std::vector<core::TermId> terms) mutable {
    std::vector<core::TermId> disequalities;
    for (std::size_t i = 0; i < terms.size(); ++i) {
      for (std::size_t j = i + 1; j < terms.size(); ++j) {
        disequalities.push_back(frontend_.build_pairwise_disequality(terms[i], terms[j]));
      }
    }
    if (disequalities.empty()) {
      resume(std::move(k), frontend_.terms_.mk_bool(true));
      return;
    }
    if (disequalities.size() == 1) {
      resume(std::move(k), disequalities.front());
      return;
    }
    resume(std::move(k), frontend_.terms_.mk_and(std::move(disequalities)));
  });
}

void TermBuilder::dispatch_plus(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() < 2) {
    throw UnsupportedInput("+ has wrong arity");
  }
  schedule_all(collect(expr, 1),
               [this, k = std::move(k)](std::vector<core::TermId> children) mutable {
                 resume(std::move(k), frontend_.terms_.mk_plus(std::move(children)));
               });
}

void TermBuilder::dispatch_minus(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() == 2) {
    schedule(expr.list[1], [this, k = std::move(k)](core::TermId x) mutable {
      resume(std::move(k), frontend_.terms_.mk_times(util::Rational(-1), x));
    });
    return;
  }
  if (expr.list.size() < 3) {
    throw UnsupportedInput("- has wrong arity");
  }
  schedule_all(collect(expr, 1),
               [this, k = std::move(k)](std::vector<core::TermId> operands) mutable {
                 std::vector<core::TermId> children;
                 children.reserve(operands.size());
                 children.push_back(operands[0]);
                 for (std::size_t i = 1; i < operands.size(); ++i) {
                   children.push_back(frontend_.terms_.mk_times(util::Rational(-1), operands[i]));
                 }
                 resume(std::move(k), frontend_.terms_.mk_plus(std::move(children)));
               });
}

void TermBuilder::dispatch_times(const smtlib::SExpr& expr, Continuation k) {
  Frontend::require_arity(expr, 3, "*");
  schedule_all(collect(expr, 1), [this,
                                  k = std::move(k)](std::vector<core::TermId> operands) mutable {
    const core::TermId lhs = operands[0];
    const core::TermId rhs = operands[1];
    const bool lhs_is_literal = frontend_.terms_.get(lhs).kind == core::TermKind::Rational;
    const bool rhs_is_literal = frontend_.terms_.get(rhs).kind == core::TermKind::Rational;
    if (lhs_is_literal && rhs_is_literal) {
      resume(std::move(k), frontend_.terms_.mk_rational(frontend_.terms_.get(lhs).rational_value *
                                                        frontend_.terms_.get(rhs).rational_value));
      return;
    }
    if (lhs_is_literal) {
      resume(std::move(k),
             frontend_.terms_.mk_times(frontend_.terms_.get(lhs).rational_value, rhs));
      return;
    }
    if (rhs_is_literal) {
      resume(std::move(k),
             frontend_.terms_.mk_times(frontend_.terms_.get(rhs).rational_value, lhs));
      return;
    }
    throw UnsupportedInput("* requires a literal rational coefficient operand (QF_LRA)");
  });
}

void TermBuilder::dispatch_divide(const smtlib::SExpr& expr, Continuation k) {
  Frontend::require_arity(expr, 3, "/");
  schedule_all(collect(expr, 1), [this,
                                  k = std::move(k)](std::vector<core::TermId> operands) mutable {
    const core::TermId lhs = operands[0];
    const core::TermId rhs = operands[1];
    if (frontend_.terms_.get(lhs).kind != core::TermKind::Rational ||
        frontend_.terms_.get(rhs).kind != core::TermKind::Rational) {
      throw UnsupportedInput("/ is only supported between rational literals in QF_LRA");
    }
    const util::Rational& denominator = frontend_.terms_.get(rhs).rational_value;
    if (denominator.is_zero()) {
      throw UnsupportedInput("division by zero is not a valid QF_LRA literal");
    }
    resume(std::move(k),
           frontend_.terms_.mk_rational(frontend_.terms_.get(lhs).rational_value / denominator));
  });
}

void TermBuilder::dispatch_comparison(const smtlib::SExpr& expr, const std::string& head,
                                      Continuation k) {
  if (expr.list.size() < 3) {
    throw UnsupportedInput(head + " has wrong arity");
  }
  const bool strict = head == "<" || head == ">";
  const bool flipped = head == ">=" || head == ">";
  schedule_all(collect(expr, 1), [this, k = std::move(k), strict,
                                  flipped](std::vector<core::TermId> operands) mutable {
    std::vector<core::TermId> atoms;
    atoms.reserve(operands.size() - 1);
    for (std::size_t i = 0; i + 1 < operands.size(); ++i) {
      const core::TermId lhs = flipped ? operands[i + 1] : operands[i];
      const core::TermId rhs = flipped ? operands[i] : operands[i + 1];
      atoms.push_back(strict ? frontend_.terms_.mk_lt(lhs, rhs) : frontend_.terms_.mk_le(lhs, rhs));
    }
    if (atoms.size() == 1) {
      resume(std::move(k), atoms.front());
      return;
    }
    resume(std::move(k), frontend_.terms_.mk_and(std::move(atoms)));
  });
}

void TermBuilder::dispatch_instantiate_definition(const std::string& name,
                                                  std::vector<core::TermId> args, Continuation k) {
  const auto found = frontend_.definitions_.find(name);
  if (found == frontend_.definitions_.end()) {
    throw UnsupportedInput("unknown definition: " + name);
  }
  if (frontend_.expanding_definitions_.contains(name)) {
    throw UnsupportedInput("recursive define-fun is unsupported: " + name);
  }
  const auto& definition = found->second;
  if (args.size() != definition.parameters.size()) {
    throw UnsupportedInput("define-fun application has wrong arity: " + name);
  }

  std::unordered_map<std::string, core::TermId> scope;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (frontend_.terms_.get(args[i]).sort != definition.parameter_sorts[i]) {
      throw UnsupportedInput("define-fun argument sort mismatch: " + name);
    }
    if (!scope.emplace(definition.parameters[i], args[i]).second) {
      throw UnsupportedInput("duplicate define-fun parameter: " + definition.parameters[i]);
    }
  }

  frontend_.expanding_definitions_.insert(name);
  frontend_.let_scopes_.push_back(std::move(scope));
  const core::SortId result_sort = definition.result_sort;
  schedule(definition.body, [this, k = std::move(k), name, result_sort](core::TermId body) mutable {
    frontend_.let_scopes_.pop_back();
    frontend_.expanding_definitions_.erase(name);
    if (frontend_.terms_.get(body).sort != result_sort) {
      throw UnsupportedInput("define-fun result sort mismatch: " + name);
    }
    resume(std::move(k), body);
  });
}

class BooleanClausifier {
 public:
  struct TheoryAtom {
    sat::Lit lit;
    core::TermId term;
    bool occurs_positive = false;
    bool occurs_negative = false;
  };

  BooleanClausifier(sat::IpasirUpBackend& backend, const core::TermManager& terms,
                    bool top_level_or_tseitin)
      : backend_(backend),
        terms_(terms),
        top_level_or_tseitin_(top_level_or_tseitin),
        lit_by_term_(terms.size()),
        theory_atom_by_term_(terms.size(), kNoTheoryAtom) {}

  void assert_formula(core::TermId term) {
    if (terms_.get(term).sort != terms_.symbols().bool_sort()) {
      throw UnsupportedInput("assertion is not Boolean");
    }
    collect_theory_atom_polarities(term, true);
    assert_positive(term);
  }

  const TheoryAtom& ensure_theory_atom(core::TermId term) {
    if (terms_.get(term).sort != terms_.symbols().bool_sort()) {
      throw UnsupportedInput("generated theory atom is not Boolean");
    }
    return theory_atom_for_term(term);
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
      case core::TermKind::Le:
      case core::TermKind::Lt:
        record_theory_atom_polarity(term_id, positive);
        return;
      case core::TermKind::Rational:
      case core::TermKind::Plus:
      case core::TermKind::Times:
        throw UnsupportedInput("Real-sorted term used in a Boolean context");
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
        if (top_level_or_tseitin_) {
          backend_.add_clause(sat::Clause{encode(term_id)});
          return;
        }
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
      case core::TermKind::Le:
      case core::TermKind::Lt:
        backend_.add_clause(sat::Clause{encode(term_id)});
        return;
      case core::TermKind::Rational:
      case core::TermKind::Plus:
      case core::TermKind::Times:
        break;
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
      case core::TermKind::Le:
      case core::TermKind::Lt:
        return theory_atom_for_term(term_id).lit;
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Not:
      case core::TermKind::Rational:
      case core::TermKind::Plus:
      case core::TermKind::Times:
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
  bool top_level_or_tseitin_ = true;
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

bool contains_application_term(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Application) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_application_term(terms, child)) {
      return true;
    }
  }
  return false;
}

// `(= a b)` is built by the sort-agnostic mk_eq for any sort, including Real.
// A plain Real equality is an LRA atom, but a Real equality containing a UF
// application is a purification definition and must dispatch to the mixed
// EUF/LRA path so EUF can own the application.
bool contains_equality(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Equality) {
    if (terms.get(term.children.at(0)).sort != terms.symbols().real_sort()) {
      return true;
    }
    if (contains_application_term(terms, term_id)) {
      return true;
    }
  }
  for (const core::TermId child : term.children) {
    if (contains_equality(terms, child)) {
      return true;
    }
  }
  return false;
}

bool contains_euf_predicate_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if ((term.kind == core::TermKind::Application || term.kind == core::TermKind::Constant) &&
      term.sort == terms.symbols().bool_sort()) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_euf_predicate_atom(terms, child)) {
      return true;
    }
  }
  return false;
}

bool contains_arithmetic_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Le || term.kind == core::TermKind::Lt) {
    return true;
  }
  if (term.kind == core::TermKind::Equality &&
      terms.get(term.children.at(0)).sort == terms.symbols().real_sort()) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_arithmetic_atom(terms, child)) {
      return true;
    }
  }
  return false;
}

struct BooleanShape {
  std::size_t or_nodes = 0;
  std::size_t not_nodes = 0;
  std::size_t top_level_or_nodes = 0;
  std::size_t max_top_level_or_children = 0;
};

void collect_boolean_shape(const core::TermManager& terms, core::TermId term_id,
                           std::unordered_set<core::TermId>& seen, BooleanShape& shape) {
  if (!seen.insert(term_id).second) {
    return;
  }
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Or) {
    ++shape.or_nodes;
  } else if (term.kind == core::TermKind::Not) {
    ++shape.not_nodes;
  }
  for (const core::TermId child : term.children) {
    collect_boolean_shape(terms, child, seen, shape);
  }
}

void collect_top_level_or_shape(const core::TermManager& terms, core::TermId term_id,
                                BooleanShape& shape) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::And) {
    for (const core::TermId child : term.children) {
      collect_top_level_or_shape(terms, child, shape);
    }
    return;
  }
  if (term.kind == core::TermKind::Or) {
    ++shape.top_level_or_nodes;
    shape.max_top_level_or_children =
        std::max(shape.max_top_level_or_children, term.children.size());
  }
}

BooleanShape boolean_shape(const core::TermManager& terms,
                           const std::vector<core::TermId>& assertions) {
  BooleanShape shape;
  std::unordered_set<core::TermId> seen;
  for (const core::TermId assertion : assertions) {
    collect_boolean_shape(terms, assertion, seen, shape);
    collect_top_level_or_shape(terms, assertion, shape);
  }
  return shape;
}

bool should_use_auto_top_level_or_tseitin(const CheckSatOptions& options,
                                          const FiniteDomainAtMostOneResult& finite_domain,
                                          const BooleanShape& shape) {
  constexpr std::size_t kMinTopLevelOrNodes = 1000;
  constexpr std::size_t kMaxTopLevelOrNodes = 2500;
  constexpr std::size_t kMaxNotNodes = 700;
  return options.auto_top_level_or_tseitin && !options.top_level_or_tseitin &&
         finite_domain.choices != 0 && !finite_domain.clauses.empty() &&
         shape.top_level_or_nodes >= kMinTopLevelOrNodes &&
         shape.top_level_or_nodes <= kMaxTopLevelOrNodes && shape.not_nodes <= kMaxNotNodes;
}

void publish_theory_atom_purity_stats(const core::TermManager& terms,
                                      const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                      util::Statistics& statistics);

void append_model_assignments(std::vector<TheoryModelAssignment>& target,
                              const std::vector<TheoryModelAssignment>& source) {
  target.insert(target.end(), source.begin(), source.end());
}

void append_boolean_model_assignments(
    std::vector<TheoryModelAssignment>& target,
    const std::vector<std::pair<core::TermId, bool>>& assignments) {
  target.reserve(target.size() + assignments.size());
  for (const auto& [term, value] : assignments) {
    target.push_back(TheoryModelAssignment{term, value, false});
  }
}

void append_equality_model_assignments(
    core::TermManager& terms, std::vector<TheoryModelAssignment>& target,
    const std::vector<std::pair<core::TermId, core::TermId>>& equalities) {
  target.reserve(target.size() + equalities.size());
  for (const auto& [lhs, rhs] : equalities) {
    target.push_back(TheoryModelAssignment{terms.mk_eq(lhs, rhs), true, false});
  }
}

CheckSatOutcome solve_boolean_conjunction(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const CheckSatOptions& options, util::Statistics& statistics,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments) {
  statistics.increment(kStatSolverBoolean);
  sat::CadicalBackend backend(options.cadical_options);
  const ActiveSolveStatsGuard active_solve_stats_guard(backend, statistics);
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }

  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
  publish_sat_stats(backend.statistics(), statistics);
  statistics.increment(kStatSatSolveCalls);
  const CheckSatResult result = map_sat_result(backend.solve());
  publish_sat_stats(backend.statistics(), statistics);
  if (result != CheckSatResult::Sat || !options.produce_model) {
    return CheckSatOutcome{result, "", statistics, ""};
  }

  std::vector<TheoryModelAssignment> model_assignments;
  model_assignments.reserve(preprocessing_model_assignments.size() +
                            clausifier.theory_atoms().size());
  append_model_assignments(model_assignments, preprocessing_model_assignments);
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    const std::optional<bool> assignment = backend.value(var);
    if (!assignment.has_value()) {
      return CheckSatOutcome{CheckSatResult::Unknown, "SAT backend did not assign theory atom",
                             statistics, ""};
    }
    const bool atom_value = atom.lit.value > 0 ? assignment.value() : !assignment.value();
    model_assignments.push_back(TheoryModelAssignment{atom.term, atom_value, false});
  }
  return CheckSatOutcome{result, "", statistics, build_qf_uf_model(terms, model_assignments)};
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

bool should_use_auto_basic_single(std::size_t assertion_count, std::size_t initial_reachable_terms,
                                  const FiniteDomainAtMostOneResult& finite_domain,
                                  const UfSymmetryBreakingResult& symmetry) {
  return assertion_count == 1 && initial_reachable_terms <= kAutoBasicSingleTermLimit &&
         finite_domain.domains >= kAutoBasicSingleMinimumDomains && symmetry.accepted_sets != 0 &&
         symmetry.terms <= kAutoBasicSingleMaximumSymmetryTerms;
}

AssignedTheoryAtom assigned_theory_atom(PureTheoryFilterMode mode,
                                        const BooleanClausifier::TheoryAtom& atom, bool value) {
  return AssignedTheoryAtom{atom.lit.value, value, atom.term,
                            should_filter_assignment(mode, atom, value)};
}

bool is_predicate_theory_atom(const core::TermManager& terms,
                              const BooleanClausifier::TheoryAtom& atom) {
  const core::Term& term = terms.get(atom.term);
  return term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort();
}

void publish_theory_atom_purity_stats(const core::TermManager& terms,
                                      const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                      util::Statistics& statistics) {
  std::size_t positive = 0;
  std::size_t negative = 0;
  std::size_t predicates = 0;
  for (const BooleanClausifier::TheoryAtom& atom : atoms) {
    if (is_pure_positive(atom)) {
      ++positive;
    } else if (is_pure_negative(atom)) {
      ++negative;
    }
    if (is_predicate_theory_atom(terms, atom)) {
      ++predicates;
    }
  }
  statistics.set(kStatPureTheoryAtomsPositive, positive);
  statistics.set(kStatPureTheoryAtomsNegative, negative);
  statistics.set(kStatEufPredicateAtoms, predicates);
}

TheoryPhaseMode resolve_theory_phase_mode(TheoryPhaseMode mode,
                                          const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                          const util::Statistics& statistics) {
  if (mode != TheoryPhaseMode::Auto) {
    return mode;
  }
  const bool has_finite_domain_structure = statistics.get(kStatFiniteDomainDomains) != 0 ||
                                           statistics.get(kStatUfSymmetryConstraints) != 0;
  if (!has_finite_domain_structure) {
    return TheoryPhaseMode::Negative;
  }
  std::size_t pure_positive = 0;
  std::size_t pure_negative = 0;
  for (const BooleanClausifier::TheoryAtom& atom : atoms) {
    if (is_pure_positive(atom)) {
      ++pure_positive;
    } else if (is_pure_negative(atom)) {
      ++pure_negative;
    }
  }
  if (pure_negative == 0) {
    return TheoryPhaseMode::Negative;
  }
  constexpr std::size_t kLowNegativePolarityLimit = 25;
  constexpr std::size_t kLowNegativeMinimumPositiveAtoms = 100;
  if (pure_negative <= kLowNegativePolarityLimit &&
      pure_positive >= kLowNegativeMinimumPositiveAtoms) {
    return TheoryPhaseMode::Polarity;
  }
  return TheoryPhaseMode::Negative;
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
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCacheBoundMisses,
                            stats.explain_cache_bound_misses);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainLength, stats.explain_length);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainTopLevelQueries,
                            stats.explain_top_level_queries);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainRecursiveQueries,
                            stats.explain_recursive_queries);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainRepeatedPairQueries,
                            stats.explain_repeated_pair_queries);
  if (stats.explain_max_recursion_depth >
      statistics.get(kStatEufIncrementalExplainMaxRecursionDepth)) {
    statistics.set(kStatEufIncrementalExplainMaxRecursionDepth, stats.explain_max_recursion_depth);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainBfsSearches,
                            stats.explain_bfs_searches);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainBfsVisited,
                            stats.explain_bfs_visited);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainBfsPathEdges,
                            stats.explain_bfs_path_edges);
  if (stats.explain_bfs_max_queue > statistics.get(kStatEufIncrementalExplainBfsMaxQueue)) {
    statistics.set(kStatEufIncrementalExplainBfsMaxQueue, stats.explain_bfs_max_queue);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestQueries,
                            stats.explain_proof_forest_queries);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestPathEdges,
                            stats.explain_proof_forest_path_edges);
  if (stats.explain_proof_forest_max_path_edges >
      statistics.get(kStatEufIncrementalExplainProofForestMaxPathEdges)) {
    statistics.set(kStatEufIncrementalExplainProofForestMaxPathEdges,
                   stats.explain_proof_forest_max_path_edges);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestMissingLca,
                            stats.explain_proof_forest_missing_lca);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestShorterThanBfs,
                            stats.explain_proof_forest_shorter_than_bfs);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestEqualToBfs,
                            stats.explain_proof_forest_equal_to_bfs);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainProofForestLongerThanBfs,
                            stats.explain_proof_forest_longer_than_bfs);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCausalEdgeSkips,
                            stats.explain_causal_edge_skips);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainDirectEdges,
                            stats.explain_direct_edges);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainCongruenceEdges,
                            stats.explain_congruence_edges);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalExplainReasonDuplicateLiterals,
                            stats.explain_reason_duplicate_literals);
  if (stats.conflict_reason_max_length >
      statistics.get(kStatEufIncrementalConflictReasonMaxLength)) {
    statistics.set(kStatEufIncrementalConflictReasonMaxLength, stats.conflict_reason_max_length);
  }
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe4,
                            stats.conflict_reason_length_le_4);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe8,
                            stats.conflict_reason_length_le_8);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe16,
                            stats.conflict_reason_length_le_16);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe32,
                            stats.conflict_reason_length_le_32);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthLe64,
                            stats.conflict_reason_length_le_64);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalConflictReasonLengthGt64,
                            stats.conflict_reason_length_gt_64);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalBacktracks, stats.backtracks);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalTrailEntries, stats.trail_entries);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalClassMemberTrailEntriesSaved,
                            stats.class_member_trail_entries_saved);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalUseListAppends, stats.use_list_appends);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalUseListDuplicateSkips,
                            stats.use_list_duplicate_skips);
  increment_stat_if_nonzero(statistics, kStatEufIncrementalAffectedUseListDedup,
                            stats.affected_use_list_dedup);
  if (stats.max_use_list_size > statistics.get(kStatEufIncrementalMaxUseListSize)) {
    statistics.set(kStatEufIncrementalMaxUseListSize, stats.max_use_list_size);
  }
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
      positive_delta(lhs.explain_cache_bound_misses, rhs.explain_cache_bound_misses),
      positive_delta(lhs.explain_length, rhs.explain_length),
      positive_delta(lhs.explain_top_level_queries, rhs.explain_top_level_queries),
      positive_delta(lhs.explain_recursive_queries, rhs.explain_recursive_queries),
      positive_delta(lhs.explain_repeated_pair_queries, rhs.explain_repeated_pair_queries),
      lhs.explain_max_recursion_depth,
      positive_delta(lhs.explain_bfs_searches, rhs.explain_bfs_searches),
      positive_delta(lhs.explain_bfs_visited, rhs.explain_bfs_visited),
      positive_delta(lhs.explain_bfs_path_edges, rhs.explain_bfs_path_edges),
      lhs.explain_bfs_max_queue,
      positive_delta(lhs.explain_proof_forest_queries, rhs.explain_proof_forest_queries),
      positive_delta(lhs.explain_proof_forest_path_edges, rhs.explain_proof_forest_path_edges),
      lhs.explain_proof_forest_max_path_edges,
      positive_delta(lhs.explain_proof_forest_missing_lca, rhs.explain_proof_forest_missing_lca),
      positive_delta(lhs.explain_proof_forest_shorter_than_bfs,
                     rhs.explain_proof_forest_shorter_than_bfs),
      positive_delta(lhs.explain_proof_forest_equal_to_bfs, rhs.explain_proof_forest_equal_to_bfs),
      positive_delta(lhs.explain_proof_forest_longer_than_bfs,
                     rhs.explain_proof_forest_longer_than_bfs),
      positive_delta(lhs.explain_causal_edge_skips, rhs.explain_causal_edge_skips),
      positive_delta(lhs.explain_direct_edges, rhs.explain_direct_edges),
      positive_delta(lhs.explain_congruence_edges, rhs.explain_congruence_edges),
      positive_delta(lhs.explain_reason_duplicate_literals, rhs.explain_reason_duplicate_literals),
      lhs.conflict_reason_max_length,
      positive_delta(lhs.conflict_reason_length_le_4, rhs.conflict_reason_length_le_4),
      positive_delta(lhs.conflict_reason_length_le_8, rhs.conflict_reason_length_le_8),
      positive_delta(lhs.conflict_reason_length_le_16, rhs.conflict_reason_length_le_16),
      positive_delta(lhs.conflict_reason_length_le_32, rhs.conflict_reason_length_le_32),
      positive_delta(lhs.conflict_reason_length_le_64, rhs.conflict_reason_length_le_64),
      positive_delta(lhs.conflict_reason_length_gt_64, rhs.conflict_reason_length_gt_64),
      positive_delta(lhs.backtracks, rhs.backtracks),
      positive_delta(lhs.trail_entries, rhs.trail_entries),
      positive_delta(lhs.class_member_trail_entries_saved, rhs.class_member_trail_entries_saved),
      positive_delta(lhs.use_list_appends, rhs.use_list_appends),
      positive_delta(lhs.use_list_duplicate_skips, rhs.use_list_duplicate_skips),
      positive_delta(lhs.affected_use_list_dedup, rhs.affected_use_list_dedup),
      lhs.max_use_list_size,
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
                     core::TermId true_term, core::TermId false_term,
                     EufExplainSearchMode explain_search_mode, bool proof_forest_diagnostics);

  theory::equality::FlatTermIndex flat;
  theory::equality::FlatCongruenceClosure base_closure;
  theory::equality::FlatTermId true_flat = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId false_flat = theory::equality::kInvalidFlatTermId;
  std::vector<FlatAssignedTheoryAtom> flat_atoms;
};

struct TermPairKey {
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;

  bool operator==(const TermPairKey& other) const = default;
};

struct TermPairKeyHash {
  std::size_t operator()(const TermPairKey& key) const {
    std::size_t seed = key.lhs + 0x9e3779b97f4a7c15ULL;
    seed ^= key.rhs + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

TermPairKey canonical_term_pair(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return TermPairKey{lhs, rhs};
}

struct FlatPairKey {
  theory::equality::FlatTermId lhs = theory::equality::kInvalidFlatTermId;
  theory::equality::FlatTermId rhs = theory::equality::kInvalidFlatTermId;

  bool operator==(const FlatPairKey& other) const = default;
};

struct FlatPairKeyHash {
  std::size_t operator()(const FlatPairKey& key) const {
    std::size_t seed = key.lhs + 0x9e3779b97f4a7c15ULL;
    seed ^= key.rhs + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

FlatPairKey canonical_flat_pair(theory::equality::FlatTermId lhs,
                                theory::equality::FlatTermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return FlatPairKey{lhs, rhs};
}

struct DynamicAckermannCandidate {
  std::size_t result_atom_index = 0;
  core::SymbolId symbol = core::kInvalidSymbolId;
  std::vector<std::size_t> argument_atom_indices;
  sat::Clause clause;
  std::size_t width = 0;
};

struct DynamicAckermannCandidateIndex {
  std::vector<DynamicAckermannCandidate> candidates;
  std::size_t result_pairs = 0;
  std::size_t missing_argument_atoms = 0;
};

bool clause_contains_lit(const sat::Clause& clause, sat::Lit lit) {
  return std::find_if(clause.begin(), clause.end(), [lit](sat::Lit existing) {
           return existing.value == lit.value;
         }) != clause.end();
}

void append_lit_once(sat::Clause& clause, sat::Lit lit) {
  if (!clause_contains_lit(clause, lit)) {
    clause.push_back(lit);
  }
}

DynamicAckermannCandidateIndex build_dynamic_ackermann_candidate_index(
    const core::TermManager& terms,
    const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms) {
  DynamicAckermannCandidateIndex index;
  std::unordered_map<TermPairKey, std::size_t, TermPairKeyHash> equality_atom_by_pair;
  equality_atom_by_pair.reserve(theory_atoms.size());
  for (std::size_t atom_index = 0; atom_index < theory_atoms.size(); ++atom_index) {
    const core::Term& term = terms.get(theory_atoms[atom_index].term);
    if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
      continue;
    }
    equality_atom_by_pair.emplace(canonical_term_pair(term.children[0], term.children[1]),
                                  atom_index);
  }

  for (std::size_t atom_index = 0; atom_index < theory_atoms.size(); ++atom_index) {
    const core::Term& result_equality = terms.get(theory_atoms[atom_index].term);
    if (result_equality.kind != core::TermKind::Equality || result_equality.children.size() != 2) {
      continue;
    }
    const core::Term& lhs = terms.get(result_equality.children[0]);
    const core::Term& rhs = terms.get(result_equality.children[1]);
    if (lhs.kind != core::TermKind::Application || rhs.kind != core::TermKind::Application ||
        lhs.symbol != rhs.symbol || lhs.children.size() != rhs.children.size()) {
      continue;
    }

    ++index.result_pairs;
    DynamicAckermannCandidate candidate;
    candidate.result_atom_index = atom_index;
    candidate.symbol = lhs.symbol;
    candidate.argument_atom_indices.reserve(lhs.children.size());
    candidate.clause.reserve(lhs.children.size() + 1U);
    candidate.clause.push_back(theory_atoms[atom_index].lit);
    std::size_t missing_argument_atoms = 0;
    for (std::size_t child_index = 0; child_index < lhs.children.size(); ++child_index) {
      const auto found = equality_atom_by_pair.find(
          canonical_term_pair(lhs.children[child_index], rhs.children[child_index]));
      if (found == equality_atom_by_pair.end()) {
        ++missing_argument_atoms;
        continue;
      }
      const sat::Lit argument_disequality_lit{-theory_atoms[found->second].lit.value};
      if (clause_contains_lit(candidate.clause, argument_disequality_lit)) {
        continue;
      }
      candidate.argument_atom_indices.push_back(found->second);
      candidate.clause.push_back(argument_disequality_lit);
    }
    if (missing_argument_atoms != 0) {
      index.missing_argument_atoms += missing_argument_atoms;
      continue;
    }
    candidate.width = candidate.clause.size();
    index.candidates.push_back(std::move(candidate));
  }
  return index;
}

void record_dynamic_ackermann_shadow(const DynamicAckermannCandidateIndex& candidate_index,
                                     const std::vector<std::optional<bool>>& atom_values,
                                     util::Statistics& statistics) {
  statistics.increment(kStatEufDynamicAckShadowInvocations);
  statistics.increment(kStatEufDynamicAckShadowResultPairs, candidate_index.result_pairs);
  statistics.increment(kStatEufDynamicAckShadowArgAtomsMissing,
                       candidate_index.missing_argument_atoms);

  for (const DynamicAckermannCandidate& candidate : candidate_index.candidates) {
    const std::size_t width = candidate.width;
    statistics.increment(kStatEufDynamicAckShadowSatVisibleClauses);
    statistics.increment(kStatEufDynamicAckShadowWidthTotal, width);
    if (width > statistics.get(kStatEufDynamicAckShadowMaxWidth)) {
      statistics.set(kStatEufDynamicAckShadowMaxWidth, width);
    }
    statistics.increment(width <= 3 ? kStatEufDynamicAckShadowWidthLe3
                                    : kStatEufDynamicAckShadowWidthGt3);

    std::size_t true_literals = 0;
    std::size_t unassigned_literals = 0;
    const std::optional<bool> result_value = atom_values.at(candidate.result_atom_index);
    if (!result_value.has_value()) {
      ++unassigned_literals;
    } else if (result_value.value()) {
      ++true_literals;
    } else {
      statistics.increment(kStatEufDynamicAckShadowResultDiseqAssigned);
    }

    bool all_arguments_equal_assigned = !candidate.argument_atom_indices.empty();
    for (const std::size_t argument_index : candidate.argument_atom_indices) {
      const std::optional<bool> argument_value = atom_values.at(argument_index);
      if (!argument_value.has_value()) {
        ++unassigned_literals;
        all_arguments_equal_assigned = false;
      } else if (argument_value.value()) {
        // The Ackermann clause contains the argument disequality literal, so an
        // assigned argument equality makes this clause literal false.
      } else {
        ++true_literals;
        all_arguments_equal_assigned = false;
      }
    }
    if (all_arguments_equal_assigned) {
      statistics.increment(kStatEufDynamicAckShadowAllArgsEqAssigned);
    }
    if (true_literals != 0) {
      statistics.increment(kStatEufDynamicAckShadowSatisfied);
    } else if (unassigned_literals == 1) {
      statistics.increment(kStatEufDynamicAckShadowUnit);
    } else if (unassigned_literals == 0) {
      statistics.increment(kStatEufDynamicAckShadowConflicting);
    }
  }
}

void record_dynamic_ackermann_shadow(const core::TermManager& terms,
                                     const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                                     const std::vector<std::optional<bool>>& atom_values,
                                     util::Statistics& statistics) {
  if (atom_values.size() != theory_atoms.size()) {
    throw UnsupportedInput("internal dynamic Ackermann shadow assignment mismatch");
  }
  const DynamicAckermannCandidateIndex candidate_index =
      build_dynamic_ackermann_candidate_index(terms, theory_atoms);
  record_dynamic_ackermann_shadow(candidate_index, atom_values, statistics);
}

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
    core::TermId true_term, core::TermId false_term, EufExplainSearchMode explain_search_mode,
    bool proof_forest_diagnostics)
    : flat(terms),
      base_closure(0, flat_explanation_search_mode(explain_search_mode), proof_forest_diagnostics) {
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
    EufExplainSearchMode explain_search_mode, bool proof_forest_diagnostics,
    util::Statistics* statistics) {
  theory::equality::FlatTermIndex flat(terms);
  const theory::equality::FlatTermId true_flat = flat.flatten(true_term);
  const theory::equality::FlatTermId false_flat = flat.flatten(false_term);
  std::vector<FlatAssignedTheoryAtom> flat_atoms;
  flat_atoms.reserve(atoms.size());
  for (const AssignedTheoryAtom& atom : atoms) {
    flat_atoms.push_back(flatten_assigned_atom(flat, terms, atom, false_flat));
  }

  theory::equality::FlatCongruenceClosure closure(
      flat.size(), flat_explanation_search_mode(explain_search_mode), proof_forest_diagnostics);
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
                                 EufExplainSearchMode explain_search_mode,
                                 bool proof_forest_diagnostics, util::Statistics& statistics,
                                 bool flush_live_stats)
      : terms_(terms),
        pure_theory_filter_(pure_theory_filter),
        flat_(terms),
        closure_(0, flat_explanation_search_mode(explain_search_mode), proof_forest_diagnostics),
        statistics_(statistics),
        flush_live_stats_(flush_live_stats) {
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
      publish_stats();
      closure_.pop(closure_checkpoint.value());
      last_published_stats_ = closure_.stats();
      ++accumulated_stats_.backtracks;
      ++unflushed_publications_;
      if (flush_live_stats_ && unflushed_publications_ >= kLiveStatsFlushInterval) {
        flush_stats();
      }
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
    ++assignments_since_publication_;
    if (flush_live_stats_ && assignments_since_publication_ >= kLiveStatsFlushInterval) {
      publish_stats();
      flush_stats();
    }
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
    publish_stats();
    add_incremental_stats(statistics_, accumulated_stats_);
    accumulated_stats_ = {};
    unflushed_publications_ = 0;
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
    ++unflushed_publications_;
    assignments_since_publication_ = 0;
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
    accumulated_stats_.explain_cache_bound_misses += stats.explain_cache_bound_misses;
    accumulated_stats_.explain_length += stats.explain_length;
    accumulated_stats_.explain_top_level_queries += stats.explain_top_level_queries;
    accumulated_stats_.explain_recursive_queries += stats.explain_recursive_queries;
    accumulated_stats_.explain_repeated_pair_queries += stats.explain_repeated_pair_queries;
    accumulated_stats_.explain_max_recursion_depth =
        std::max(accumulated_stats_.explain_max_recursion_depth, stats.explain_max_recursion_depth);
    accumulated_stats_.explain_bfs_searches += stats.explain_bfs_searches;
    accumulated_stats_.explain_bfs_visited += stats.explain_bfs_visited;
    accumulated_stats_.explain_bfs_path_edges += stats.explain_bfs_path_edges;
    accumulated_stats_.explain_bfs_max_queue =
        std::max(accumulated_stats_.explain_bfs_max_queue, stats.explain_bfs_max_queue);
    accumulated_stats_.explain_proof_forest_queries += stats.explain_proof_forest_queries;
    accumulated_stats_.explain_proof_forest_path_edges += stats.explain_proof_forest_path_edges;
    accumulated_stats_.explain_proof_forest_max_path_edges =
        std::max(accumulated_stats_.explain_proof_forest_max_path_edges,
                 stats.explain_proof_forest_max_path_edges);
    accumulated_stats_.explain_proof_forest_missing_lca += stats.explain_proof_forest_missing_lca;
    accumulated_stats_.explain_proof_forest_shorter_than_bfs +=
        stats.explain_proof_forest_shorter_than_bfs;
    accumulated_stats_.explain_proof_forest_equal_to_bfs += stats.explain_proof_forest_equal_to_bfs;
    accumulated_stats_.explain_proof_forest_longer_than_bfs +=
        stats.explain_proof_forest_longer_than_bfs;
    accumulated_stats_.explain_causal_edge_skips += stats.explain_causal_edge_skips;
    accumulated_stats_.explain_direct_edges += stats.explain_direct_edges;
    accumulated_stats_.explain_congruence_edges += stats.explain_congruence_edges;
    accumulated_stats_.explain_reason_duplicate_literals += stats.explain_reason_duplicate_literals;
    accumulated_stats_.conflict_reason_max_length =
        std::max(accumulated_stats_.conflict_reason_max_length, stats.conflict_reason_max_length);
    accumulated_stats_.conflict_reason_length_le_4 += stats.conflict_reason_length_le_4;
    accumulated_stats_.conflict_reason_length_le_8 += stats.conflict_reason_length_le_8;
    accumulated_stats_.conflict_reason_length_le_16 += stats.conflict_reason_length_le_16;
    accumulated_stats_.conflict_reason_length_le_32 += stats.conflict_reason_length_le_32;
    accumulated_stats_.conflict_reason_length_le_64 += stats.conflict_reason_length_le_64;
    accumulated_stats_.conflict_reason_length_gt_64 += stats.conflict_reason_length_gt_64;
    accumulated_stats_.backtracks += stats.backtracks;
    accumulated_stats_.trail_entries += stats.trail_entries;
    accumulated_stats_.class_member_trail_entries_saved += stats.class_member_trail_entries_saved;
    accumulated_stats_.use_list_appends += stats.use_list_appends;
    accumulated_stats_.use_list_duplicate_skips += stats.use_list_duplicate_skips;
    accumulated_stats_.affected_use_list_dedup += stats.affected_use_list_dedup;
    accumulated_stats_.max_use_list_size =
        std::max(accumulated_stats_.max_use_list_size, stats.max_use_list_size);
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
  bool flush_live_stats_ = false;
  std::size_t unflushed_publications_ = 0;
  std::size_t assignments_since_publication_ = 0;
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
class TheoryConflictTerminator final : public CaDiCaL::Terminator {
 public:
  TheoryConflictTerminator(util::Statistics& statistics, std::size_t conflict_limit)
      : statistics_(statistics), conflict_limit_(conflict_limit) {}

  bool terminate() override {
    if (statistics_.get(kStatTheoryConflicts) < conflict_limit_) {
      return false;
    }
    reached_ = true;
    return true;
  }

  bool reached() const { return reached_; }

 private:
  util::Statistics& statistics_;
  std::size_t conflict_limit_ = 0;
  bool reached_ = false;
};

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
    int max_theory_var = 0;
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      max_theory_var =
          std::max(max_theory_var, atom.lit.value > 0 ? atom.lit.value : -atom.lit.value);
    }
    observed_vars_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    atom_by_var_.assign(static_cast<std::size_t>(max_theory_var) + 1, nullptr);
    assignment_by_var_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    pending_vars_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    queued_candidate_indices_.assign(theory_atoms_.size(), 0);
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      const std::size_t index = static_cast<std::size_t>(var);
      observed_vars_[index] = 1;
      atom_by_var_[index] = &atom;
    }
    if (options_.euf_dynamic_ackermann_shadow || options_.euf_diseq_ackermann) {
      dynamic_ackermann_candidates_ =
          build_dynamic_ackermann_candidate_index(terms_, theory_atoms_);
    }
    if (options_.euf_diseq_ackermann && options_.euf_propagation != EufPropagationMode::Conflict) {
      build_diseq_ackermann_watch_index();
    }
    if (options_.euf_engine == EufEngine::Incremental) {
      incremental_state_.emplace(terms_, theory_atoms_, true_term_, false_term_,
                                 options_.pure_theory_filter, options_.euf_explain_search,
                                 options_.euf_proof_forest_diagnostics, statistics_,
                                 options_.live_statistics != nullptr);
      build_candidate_index();
    }
  }

#if PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR
  void notify_assignment(const std::vector<int>& lits) override {
    for (const int lit : lits) {
      notify_assignment_lit(lit);
    }
    finish_assignment_notification();
  }
#else
  void notify_assignment(int lit, bool /*is_fixed*/) override {
    notify_assignment_lit(lit);
    finish_assignment_notification();
  }
#endif

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
      assignment_by_var_[static_cast<std::size_t>(var)] = 0;
      --assigned_var_count_;
      clear_pending_var(var);
    }
    level_starts_.resize(std::min<std::size_t>(new_level + 1, level_starts_.size()));
    if (level_starts_.empty()) {
      level_starts_.push_back(0);
    }
    if (incremental_state_.has_value()) {
      incremental_state_->pop_to_level(new_level);
    }
    clear_pending_queue();
    clear_all_pending_vars();
    candidate_queue_.clear();
    clear_all_candidate_marks();
    diseq_ackermann_queue_.clear();
    clear_all_diseq_ackermann_marks();
    external_clause_.clear();
    external_clause_index_ = 0;
  }

  bool cb_check_found_model(const std::vector<int>& /*model*/) override { return true; }

  void flush_incremental_stats() {
    if (incremental_state_.has_value()) {
      incremental_state_->flush_stats();
    }
  }

  // The propagator's incremental closure already asserts and checks every
  // observed-variable assignment synchronously via notify_assignment, and any
  // conflict it finds is delivered to CaDiCaL through cb_has_external_clause
  // before search can report SAT. So once backend.solve() returns SAT, this
  // closure's conflict state is already the authoritative answer for the
  // current full assignment; the caller does not need to rebuild a second
  // closure from scratch to re-derive it.
  std::optional<sat::Clause> incremental_conflict() const {
    return incremental_state_.has_value() ? incremental_state_->conflict() : std::nullopt;
  }

  void record_dynamic_ackermann_shadow_round() {
    std::vector<std::optional<bool>> atom_values;
    atom_values.reserve(theory_atoms_.size());
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (!is_assigned_var(var)) {
        atom_values.push_back(std::nullopt);
        continue;
      }
      const bool assignment = assigned_value(var);
      atom_values.push_back(atom.lit.value > 0 ? assignment : !assignment);
    }
    record_dynamic_ackermann_shadow(dynamic_ackermann_candidates_, atom_values, statistics_);
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
    clear_pending_var(lit > 0 ? lit : -lit);
    if (const auto found = reasons_.find(lit);
        found != reasons_.end() && found->second.source == ReasonSource::DiseqAckermann) {
      statistics_.increment(kStatEufDiseqAckReturned);
    }
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

#if PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR
  bool cb_has_external_clause(bool& is_forgettable) override {
    maybe_store_conflict();
    maybe_store_diseq_ackermann_conflict();
    if (external_clause_.empty()) {
      return false;
    }
    is_forgettable = false;
    return true;
  }
#else
  bool cb_has_external_clause() override {
    maybe_store_conflict();
    maybe_store_diseq_ackermann_conflict();
    return !external_clause_.empty();
  }
#endif

  int cb_add_external_clause_lit() override {
    if (external_clause_index_ >= external_clause_.size()) {
      external_clause_.clear();
      external_clause_index_ = 0;
      return 0;
    }
    return external_clause_[external_clause_index_++].value;
  }

 private:
  enum class ReasonSource {
    EufPropagation,
    DiseqAckermann,
  };

  struct Reason {
    std::vector<int> antecedents;
    std::size_t index = 0;
    int active_lit = 0;
    ReasonSource source = ReasonSource::EufPropagation;
  };

  const BooleanClausifier::TheoryAtom* atom_for_var(int var) const {
    if (var < 0 || static_cast<std::size_t>(var) >= atom_by_var_.size()) {
      return nullptr;
    }
    return atom_by_var_[static_cast<std::size_t>(var)];
  }

  bool is_observed_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < observed_vars_.size() &&
           observed_vars_[static_cast<std::size_t>(var)] != 0;
  }

  bool is_assigned_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < assignment_by_var_.size() &&
           assignment_by_var_[static_cast<std::size_t>(var)] != 0;
  }

  bool assigned_value(int var) const {
    return assignment_by_var_[static_cast<std::size_t>(var)] > 0;
  }

  bool literal_is_true(sat::Lit lit) const {
    const int var = lit.value > 0 ? lit.value : -lit.value;
    if (!is_assigned_var(var)) {
      return false;
    }
    const bool assignment = assigned_value(var);
    return lit.value > 0 ? assignment : !assignment;
  }

  bool literal_is_false(sat::Lit lit) const {
    const int var = lit.value > 0 ? lit.value : -lit.value;
    if (!is_assigned_var(var)) {
      return false;
    }
    return !literal_is_true(lit);
  }

  bool is_pending_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < pending_vars_.size() &&
           pending_vars_[static_cast<std::size_t>(var)] != 0;
  }

  void clear_pending_var(int var) {
    if (var >= 0 && static_cast<std::size_t>(var) < pending_vars_.size()) {
      pending_vars_[static_cast<std::size_t>(var)] = 0;
    }
  }

  void clear_all_pending_vars() { std::fill(pending_vars_.begin(), pending_vars_.end(), 0); }

  void clear_all_candidate_marks() {
    std::fill(queued_candidate_indices_.begin(), queued_candidate_indices_.end(), 0);
  }

  void clear_all_diseq_ackermann_marks() {
    std::fill(queued_diseq_ackermann_candidates_.begin(), queued_diseq_ackermann_candidates_.end(),
              0);
  }

  void notify_assignment_lit(int lit) {
    const int var = lit > 0 ? lit : -lit;
    if (!is_observed_var(var)) {
      return;
    }
    if (is_assigned_var(var)) {
      return;
    }
    assignment_by_var_[static_cast<std::size_t>(var)] = lit > 0 ? 1 : -1;
    ++assigned_var_count_;
    trail_.push_back(lit);
    clear_pending_var(var);
    enqueue_diseq_ackermann_candidates(var);
    if (!incremental_state_.has_value()) {
      return;
    }
    const BooleanClausifier::TheoryAtom* atom = atom_for_var(var);
    if (atom == nullptr) {
      return;
    }
    const bool atom_value = atom->lit.value > 0 ? lit > 0 : lit < 0;
    statistics_.increment(kStatEufAssignments);
    const core::Term& atom_term = terms_.get(atom->term);
    if (atom_term.kind == core::TermKind::Equality) {
      statistics_.increment(atom_value ? kStatEufEqAssignments : kStatEufDiseqAssignments);
    }
    AssignedTheoryAtom assigned =
        assigned_theory_atom(options_.pure_theory_filter, *atom, atom_value);
    count_filtered_assignment(assigned, statistics_);
    incremental_state_->assert_assignment(assigned);
    enqueue_changed_candidates();
  }

  void clear_pending_queue() {
    for (const int lit : pending_) {
      const auto found = reasons_.find(lit);
      if (found != reasons_.end() && found->second.source == ReasonSource::DiseqAckermann) {
        statistics_.increment(kStatEufDiseqAckPendingCleared);
      }
    }
    pending_.clear();
  }

  void finish_assignment_notification() { clear_pending_queue(); }

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
    if (queued_candidate_indices_[atom_index] == 0) {
      queued_candidate_indices_[atom_index] = 1;
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

  void build_diseq_ackermann_watch_index() {
    queued_diseq_ackermann_candidates_.assign(dynamic_ackermann_candidates_.candidates.size(), 0);
    diseq_ackermann_indices_by_var_.assign(observed_vars_.size(), {});
    statistics_.increment(kStatEufDiseqAckMissingArgAtom,
                          dynamic_ackermann_candidates_.missing_argument_atoms);
    std::unordered_map<core::SymbolId, std::size_t> candidates_by_symbol;
    candidates_by_symbol.reserve(dynamic_ackermann_candidates_.candidates.size());
    std::size_t watched_candidates = 0;
    for (std::size_t candidate_index = 0;
         candidate_index < dynamic_ackermann_candidates_.candidates.size(); ++candidate_index) {
      const DynamicAckermannCandidate& candidate =
          dynamic_ackermann_candidates_.candidates[candidate_index];
      if (candidate.width > options_.euf_diseq_ackermann_max_width) {
        statistics_.increment(kStatEufDiseqAckWidthSkips);
        continue;
      }
      std::size_t& symbol_count = candidates_by_symbol[candidate.symbol];
      if (symbol_count >= options_.euf_diseq_ackermann_per_symbol) {
        statistics_.increment(kStatEufDiseqAckPerSymbolCutoffs);
        continue;
      }
      ++symbol_count;
      ++watched_candidates;
      for (const sat::Lit lit : candidate.clause) {
        const int var = lit.value > 0 ? lit.value : -lit.value;
        if (var < 0 || static_cast<std::size_t>(var) >= diseq_ackermann_indices_by_var_.size()) {
          continue;
        }
        std::vector<std::size_t>& watched =
            diseq_ackermann_indices_by_var_[static_cast<std::size_t>(var)];
        if (std::find(watched.begin(), watched.end(), candidate_index) == watched.end()) {
          watched.push_back(candidate_index);
        }
      }
    }
    statistics_.set(kStatEufDiseqAckCandidates, watched_candidates);
  }

  void enqueue_diseq_ackermann_candidate(std::size_t candidate_index) {
    if (candidate_index >= queued_diseq_ackermann_candidates_.size()) {
      return;
    }
    if (queued_diseq_ackermann_candidates_[candidate_index] != 0) {
      return;
    }
    queued_diseq_ackermann_candidates_[candidate_index] = 1;
    diseq_ackermann_queue_.push_back(candidate_index);
    statistics_.increment(kStatEufDiseqAckQueuePushes);
  }

  void enqueue_diseq_ackermann_candidates(int var) {
    if (!options_.euf_diseq_ackermann || var < 0 ||
        static_cast<std::size_t>(var) >= diseq_ackermann_indices_by_var_.size()) {
      return;
    }
    for (const std::size_t candidate_index :
         diseq_ackermann_indices_by_var_[static_cast<std::size_t>(var)]) {
      enqueue_diseq_ackermann_candidate(candidate_index);
    }
  }

  std::vector<AssignedTheoryAtom> current_assigned_atoms() const {
    std::vector<AssignedTheoryAtom> atoms;
    atoms.reserve(assigned_var_count_);
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (!is_assigned_var(var)) {
        continue;
      }
      const bool assignment = assigned_value(var);
      const bool atom_value = atom.lit.value > 0 ? assignment : !assignment;
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
      if (!is_observed_var(var)) {
        return false;
      }
      if (!is_assigned_var(var)) {
        return false;
      }
      const bool assignment = assigned_value(var);
      const bool literal_is_true = lit.value > 0 ? assignment : !assignment;
      if (literal_is_true) {
        return false;
      }
    }
    return propagated_count == 1;
  }

  bool store_reason(int propagated_lit, const sat::Clause& clause,
                    ReasonSource source = ReasonSource::EufPropagation) {
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
    reasons_[propagated_lit] = Reason{std::move(antecedents), 0, 0, source};
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
                                                false_term_, options_.euf_explain_search,
                                                options_.euf_proof_forest_diagnostics, &statistics_)
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
    if (options_.euf_dynamic_ackermann_shadow) {
      record_dynamic_ackermann_shadow_round();
    }
    compute_pending_diseq_ackermann_propagations();
    if (!pending_.empty() || !external_clause_.empty()) {
      return;
    }
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
      if (is_assigned_var(var) || is_pending_var(var)) {
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
      queued_candidate_indices_[atom_index] = 0;
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
    if (var >= 0 && static_cast<std::size_t>(var) < pending_vars_.size()) {
      pending_vars_[static_cast<std::size_t>(var)] = 1;
    }
    statistics_.increment(kStatEufPropagationDelivered);
    statistics_.increment(kStatEufPropagationReasonLits,
                          reasons_[propagated_lit].antecedents.size());
    return true;
  }

  struct DiseqAckermannClauseState {
    bool active = false;
    bool satisfied = false;
    bool conflict = false;
    int propagated_lit = 0;
  };

  DiseqAckermannClauseState evaluate_diseq_ackermann_candidate(
      const DynamicAckermannCandidate& candidate) const {
    if (candidate.clause.empty() || !literal_is_false(candidate.clause.front())) {
      return DiseqAckermannClauseState{};
    }

    DiseqAckermannClauseState state;
    state.active = true;
    std::size_t unassigned_literals = 0;
    int unassigned_lit = 0;
    for (std::size_t lit_index = 1; lit_index < candidate.clause.size(); ++lit_index) {
      const sat::Lit lit = candidate.clause[lit_index];
      const int var = lit.value > 0 ? lit.value : -lit.value;
      if (literal_is_true(lit)) {
        state.satisfied = true;
        return state;
      }
      if (!is_assigned_var(var)) {
        if (is_pending_var(var)) {
          return state;
        }
        ++unassigned_literals;
        unassigned_lit = lit.value;
      }
    }

    if (unassigned_literals == 0) {
      state.conflict = true;
    } else if (unassigned_literals == 1) {
      state.propagated_lit = unassigned_lit;
    }
    return state;
  }

  bool store_diseq_ackermann_propagation(const DynamicAckermannCandidate& candidate,
                                         int propagated_lit) {
    const int var = propagated_lit > 0 ? propagated_lit : -propagated_lit;
    if (!store_reason(propagated_lit, candidate.clause, ReasonSource::DiseqAckermann)) {
      return false;
    }
    pending_.push_back(propagated_lit);
    if (var >= 0 && static_cast<std::size_t>(var) < pending_vars_.size()) {
      pending_vars_[static_cast<std::size_t>(var)] = 1;
    }
    statistics_.increment(kStatEufDiseqAckPropagated);
    statistics_.increment(kStatEufDiseqAckReasonLits, reasons_[propagated_lit].antecedents.size());
    statistics_.increment(kStatEufPropagationDelivered);
    statistics_.increment(kStatEufPropagationReasonLits,
                          reasons_[propagated_lit].antecedents.size());
    return true;
  }

  void compute_pending_diseq_ackermann_propagations() {
    if (!options_.euf_diseq_ackermann || diseq_ackermann_queue_.empty()) {
      return;
    }

    const std::optional<std::size_t> delivery_limit =
        resolve_delivery_budget(options_.euf_propagation_delivery,
                                options_.euf_diseq_ackermann_queue_budget, theory_atoms_.size());
    std::size_t inspected = 0;
    std::size_t delivered = 0;
    while (!diseq_ackermann_queue_.empty()) {
      if (inspected >= options_.euf_diseq_ackermann_queue_budget) {
        statistics_.increment(kStatEufDiseqAckQueueBudgetCutoffs);
        break;
      }
      if (delivery_limit.has_value() && delivered >= delivery_limit.value()) {
        break;
      }
      const std::size_t candidate_index = diseq_ackermann_queue_.front();
      diseq_ackermann_queue_.pop_front();
      if (candidate_index < queued_diseq_ackermann_candidates_.size()) {
        queued_diseq_ackermann_candidates_[candidate_index] = 0;
      }
      if (candidate_index >= dynamic_ackermann_candidates_.candidates.size()) {
        continue;
      }
      ++inspected;
      statistics_.increment(kStatEufDiseqAckQueuePops);
      const DynamicAckermannCandidate& candidate =
          dynamic_ackermann_candidates_.candidates[candidate_index];
      const DiseqAckermannClauseState state = evaluate_diseq_ackermann_candidate(candidate);
      if (!state.active) {
        continue;
      }
      if (state.satisfied) {
        statistics_.increment(kStatEufDiseqAckSatisfiedSkips);
        continue;
      }
      if (state.conflict) {
        if (external_clause_.empty()) {
          external_clause_ = candidate.clause;
          external_clause_index_ = 0;
          statistics_.increment(kStatEufDiseqAckConflict);
          statistics_.increment(kStatTheoryConflicts);
        }
        break;
      }
      if (state.propagated_lit == 0) {
        continue;
      }
      statistics_.increment(kStatEufDiseqAckUnit);
      if (store_diseq_ackermann_propagation(candidate, state.propagated_lit)) {
        ++delivered;
      }
    }
    statistics_.increment(kStatEufPropagationCandidates, inspected);
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

  void maybe_store_diseq_ackermann_conflict() {
    if (!external_clause_.empty() || !options_.euf_diseq_ackermann ||
        diseq_ackermann_queue_.empty()) {
      return;
    }
    std::size_t inspected = 0;
    while (!diseq_ackermann_queue_.empty()) {
      if (inspected >= options_.euf_diseq_ackermann_queue_budget) {
        statistics_.increment(kStatEufDiseqAckQueueBudgetCutoffs);
        return;
      }
      const std::size_t candidate_index = diseq_ackermann_queue_.front();
      diseq_ackermann_queue_.pop_front();
      if (candidate_index < queued_diseq_ackermann_candidates_.size()) {
        queued_diseq_ackermann_candidates_[candidate_index] = 0;
      }
      if (candidate_index >= dynamic_ackermann_candidates_.candidates.size()) {
        continue;
      }
      ++inspected;
      statistics_.increment(kStatEufDiseqAckQueuePops);
      const DynamicAckermannCandidate& candidate =
          dynamic_ackermann_candidates_.candidates[candidate_index];
      const DiseqAckermannClauseState state = evaluate_diseq_ackermann_candidate(candidate);
      if (!state.active) {
        continue;
      }
      if (state.satisfied) {
        statistics_.increment(kStatEufDiseqAckSatisfiedSkips);
        continue;
      }
      if (!state.conflict) {
        if (state.propagated_lit != 0) {
          statistics_.increment(kStatEufDiseqAckUnit);
          if (store_diseq_ackermann_propagation(candidate, state.propagated_lit)) {
            return;
          }
        }
        continue;
      }
      external_clause_ = candidate.clause;
      external_clause_index_ = 0;
      statistics_.increment(kStatEufDiseqAckConflict);
      statistics_.increment(kStatTheoryConflicts);
      return;
    }
  }

  const core::TermManager& terms_;
  const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms_;
  core::TermId true_term_ = core::kInvalidTermId;
  core::TermId false_term_ = core::kInvalidTermId;
  const CheckSatOptions& options_;
  bool adaptive_propagation_ = false;
  util::Statistics& statistics_;
  std::vector<unsigned char> observed_vars_;
  std::vector<const BooleanClausifier::TheoryAtom*> atom_by_var_;
  std::vector<signed char> assignment_by_var_;
  std::size_t assigned_var_count_ = 0;
  std::vector<int> trail_;
  std::vector<std::size_t> level_starts_{0};
  std::deque<int> pending_;
  std::vector<unsigned char> pending_vars_;
  std::deque<std::size_t> candidate_queue_;
  std::vector<unsigned char> queued_candidate_indices_;
  std::vector<std::vector<std::size_t>> candidate_indices_by_flat_term_;
  std::vector<theory::equality::FlatTermId> changed_terms_scratch_;
  std::unordered_map<int, Reason> reasons_;
  DynamicAckermannCandidateIndex dynamic_ackermann_candidates_;
  std::deque<std::size_t> diseq_ackermann_queue_;
  std::vector<unsigned char> queued_diseq_ackermann_candidates_;
  std::vector<std::vector<std::size_t>> diseq_ackermann_indices_by_var_;
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
            ? flat_model_propagation_clause_for(terms, atoms, i, true_term, false_term,
                                                options.euf_explain_search,
                                                options.euf_proof_forest_diagnostics, &statistics)
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

struct EufClosureCheckResult {
  std::optional<sat::Clause> conflict;
  theory::equality::FlatCongruenceClosure closure;
};

EufClosureCheckResult check_incremental_euf_closure(const PreparedFlatTheory& prepared,
                                                    const std::vector<AssignedTheoryAtom>& atoms,
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
  return EufClosureCheckResult{closure.conflict(), std::move(closure)};
}

bool theory_atom_occurs_in_formula(const BooleanClausifier& clausifier, core::TermId term) {
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    if (atom.term == term && (atom.occurs_positive || atom.occurs_negative)) {
      return true;
    }
  }
  return false;
}

std::optional<sat::Lit> create_interface_equality_lit(const core::TermManager& terms,
                                                      BooleanClausifier& clausifier,
                                                      TheoryCombinationManager& manager,
                                                      core::TermId equality) {
  const core::Term& term = terms.get(equality);
  if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
    return std::nullopt;
  }
  if (const std::optional<sat::Lit> existing = manager.interface_equality_lit(equality);
      existing.has_value()) {
    return existing;
  }
  if (!manager.interface_equality(equality).has_value()) {
    return std::nullopt;
  }
  const BooleanClausifier::TheoryAtom& theory_atom = clausifier.ensure_theory_atom(equality);
  if (!manager.register_interface_equality_atom(equality, theory_atom.lit)) {
    return std::nullopt;
  }
  return theory_atom.lit;
}

std::optional<bool> lit_value_in_backend(const sat::IpasirUpBackend& backend, sat::Lit lit) {
  const int var = lit.value > 0 ? lit.value : -lit.value;
  const std::optional<bool> assignment = backend.value(var);
  if (!assignment.has_value()) {
    return std::nullopt;
  }
  return lit.value > 0 ? assignment.value() : !assignment.value();
}

bool is_lit_true_in_backend(const sat::IpasirUpBackend& backend, sat::Lit lit) {
  return lit_value_in_backend(backend, lit).value_or(false);
}

std::size_t registered_interface_equality_atom_count(const TheoryCombinationManager& manager) {
  std::size_t count = 0;
  for (const InterfaceEqualityRegistryEntry& atom : manager.summary().equality_registry) {
    if (atom.lit.value != 0) {
      ++count;
    }
  }
  return count;
}

std::vector<InterfaceEqualityRegistryEntry> collect_true_interface_equalities(
    const sat::IpasirUpBackend& backend, const TheoryCombinationManager& manager) {
  std::vector<InterfaceEqualityRegistryEntry> true_atoms;
  true_atoms.reserve(manager.summary().equality_registry.size());
  for (const InterfaceEqualityRegistryEntry& atom : manager.summary().equality_registry) {
    if (atom.lit.value != 0 && is_lit_true_in_backend(backend, atom.lit)) {
      true_atoms.push_back(atom);
    }
  }
  return true_atoms;
}

std::vector<InterfaceEqualityRegistryEntry> collect_false_interface_equalities(
    const sat::IpasirUpBackend& backend, const TheoryCombinationManager& manager) {
  std::vector<InterfaceEqualityRegistryEntry> false_atoms;
  false_atoms.reserve(manager.summary().equality_registry.size());
  for (const InterfaceEqualityRegistryEntry& atom : manager.summary().equality_registry) {
    if (atom.lit.value == 0) {
      continue;
    }
    const std::optional<bool> value = lit_value_in_backend(backend, atom.lit);
    if (value.has_value() && !value.value()) {
      false_atoms.push_back(atom);
    }
  }
  return false_atoms;
}

void append_interface_equalities(
    const std::vector<InterfaceEqualityRegistryEntry>& interface_equalities, bool value,
    std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
    std::vector<AssignedTheoryAtom>& assignments) {
  theory_atoms.reserve(theory_atoms.size() + interface_equalities.size());
  assignments.reserve(assignments.size() + interface_equalities.size());
  for (const InterfaceEqualityRegistryEntry& atom : interface_equalities) {
    theory_atoms.push_back(BooleanClausifier::TheoryAtom{atom.lit, atom.atom, false, false});
    assignments.push_back(AssignedTheoryAtom{atom.lit.value, value, atom.atom, false});
  }
}

void append_true_interface_equalities(
    const std::vector<InterfaceEqualityRegistryEntry>& interface_equalities,
    std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
    std::vector<AssignedTheoryAtom>& assignments) {
  append_interface_equalities(interface_equalities, true, theory_atoms, assignments);
}

bool force_implied_interface_equalities(
    const core::TermManager& terms, BooleanClausifier& clausifier, sat::IpasirUpBackend& backend,
    TheoryCombinationManager& manager, const std::vector<core::TermId>& implied_equalities,
    const std::vector<AssignedTheoryAtom>& reason_assignments, util::Statistics& statistics) {
  std::vector<sat::Clause> clauses_to_learn;
  clauses_to_learn.reserve(implied_equalities.size());
  std::vector<core::TermId> equalities_to_create;
  equalities_to_create.reserve(implied_equalities.size());
  const auto make_reason_clause = [&](sat::Lit implied_lit) {
    sat::Clause clause;
    clause.reserve(reason_assignments.size() + 1);
    for (const AssignedTheoryAtom& atom : reason_assignments) {
      if (!atom.filtered) {
        append_lit_once(clause, blocking_lit(atom));
      }
    }
    append_lit_once(clause, implied_lit);
    return clause;
  };
  for (const core::TermId equality : implied_equalities) {
    const std::optional<sat::Lit> lit = manager.interface_equality_lit(equality);
    if (!lit.has_value()) {
      equalities_to_create.push_back(equality);
      continue;
    }
    if (is_lit_true_in_backend(backend, lit.value())) {
      continue;
    }
    clauses_to_learn.push_back(make_reason_clause(lit.value()));
  }
  for (const core::TermId equality : equalities_to_create) {
    const std::optional<sat::Lit> lit =
        create_interface_equality_lit(terms, clausifier, manager, equality);
    if (!lit.has_value()) {
      continue;
    }
    clauses_to_learn.push_back(make_reason_clause(lit.value()));
  }
  for (const sat::Clause& clause : clauses_to_learn) {
    backend.add_clause(clause);
  }
  statistics.set(":no.interface.sat-equality-atoms",
                 registered_interface_equality_atom_count(manager));
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  statistics.increment(":no.interface.sat-equalities-forced", clauses_to_learn.size());
  return !clauses_to_learn.empty();
}

bool clause_mentions_interface_equality(const sat::Clause& clause,
                                        const TheoryCombinationManager& manager) {
  for (const sat::Lit lit : clause) {
    const int var = lit.value > 0 ? lit.value : -lit.value;
    for (const InterfaceEqualityRegistryEntry& atom : manager.summary().equality_registry) {
      if (atom.lit.value == 0) {
        continue;
      }
      const int atom_var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (var == atom_var) {
        return true;
      }
    }
  }
  return false;
}

void record_interface_conflict(const std::optional<sat::Clause>& conflict,
                               const TheoryCombinationManager& manager, std::string_view source_key,
                               util::Statistics& statistics) {
  if (!conflict.has_value() || !clause_mentions_interface_equality(conflict.value(), manager)) {
    return;
  }
  statistics.increment(":no.interface.conflicts");
  statistics.increment(source_key);
}

std::size_t unordered_pair_count(std::size_t size) {
  if (size < 2) {
    return 0;
  }
  return (size * (size - 1U)) / 2U;
}

struct EufCarePairDiagnostics {
  std::size_t full_pairs = 0;
  std::vector<TermPairKey> term_pairs;
};

EufCarePairDiagnostics euf_care_pair_diagnostics(
    const PreparedFlatTheory& prepared, theory::equality::FlatCongruenceClosure& closure,
    const std::vector<core::TermId>& shared_real_terms) {
  EufCarePairDiagnostics diagnostics;
  diagnostics.full_pairs = unordered_pair_count(shared_real_terms.size());

  std::unordered_map<theory::equality::FlatTermId, core::TermId> shared_term_by_flat;
  shared_term_by_flat.reserve(shared_real_terms.size());
  for (const core::TermId term : shared_real_terms) {
    if (const std::optional<theory::equality::FlatTermId> flat =
            prepared.flat.existing_flat_term(term);
        flat.has_value()) {
      shared_term_by_flat.emplace(flat.value(), term);
    }
  }

  struct SharedArgumentDefinition {
    theory::equality::FlatTermId function = theory::equality::kInvalidFlatTermId;
    theory::equality::FlatTermId argument = theory::equality::kInvalidFlatTermId;
  };

  std::vector<SharedArgumentDefinition> shared_argument_definitions;
  shared_argument_definitions.reserve(prepared.flat.definitions().size());
  for (const theory::equality::FlatDefinition& definition : prepared.flat.definitions()) {
    if (shared_term_by_flat.contains(definition.argument)) {
      shared_argument_definitions.push_back(
          SharedArgumentDefinition{definition.function, definition.argument});
    }
  }

  std::unordered_set<FlatPairKey, FlatPairKeyHash> candidate_pairs;
  for (std::size_t i = 0; i < shared_argument_definitions.size(); ++i) {
    const SharedArgumentDefinition& lhs = shared_argument_definitions[i];
    for (std::size_t j = i + 1; j < shared_argument_definitions.size(); ++j) {
      const SharedArgumentDefinition& rhs = shared_argument_definitions[j];
      if (lhs.argument == rhs.argument || !closure.are_equal(lhs.function, rhs.function)) {
        continue;
      }
      candidate_pairs.insert(canonical_flat_pair(lhs.argument, rhs.argument));
    }
  }

  diagnostics.term_pairs.reserve(candidate_pairs.size());
  for (const FlatPairKey& pair : candidate_pairs) {
    const auto lhs = shared_term_by_flat.find(pair.lhs);
    const auto rhs = shared_term_by_flat.find(pair.rhs);
    if (lhs != shared_term_by_flat.end() && rhs != shared_term_by_flat.end()) {
      diagnostics.term_pairs.push_back(canonical_term_pair(lhs->second, rhs->second));
    }
  }
  std::sort(diagnostics.term_pairs.begin(), diagnostics.term_pairs.end(),
            [](const TermPairKey& lhs, const TermPairKey& rhs) {
              return lhs.lhs == rhs.lhs ? lhs.rhs < rhs.rhs : lhs.lhs < rhs.lhs;
            });
  diagnostics.term_pairs.erase(
      std::unique(diagnostics.term_pairs.begin(), diagnostics.term_pairs.end()),
      diagnostics.term_pairs.end());
  return diagnostics;
}

void publish_euf_care_pair_diagnostics(const EufCarePairDiagnostics& diagnostics,
                                       util::Statistics& statistics) {
  statistics.increment(":no.interface.euf-care-checks");
  statistics.set(":no.interface.euf-care-full-pairs", diagnostics.full_pairs);
  statistics.set(":no.interface.euf-care-pairs", diagnostics.term_pairs.size());
  statistics.set(":no.interface.euf-care-pruned-pairs",
                 diagnostics.full_pairs > diagnostics.term_pairs.size()
                     ? diagnostics.full_pairs - diagnostics.term_pairs.size()
                     : 0);
}

bool is_real_equality_term(const core::TermManager& terms, const core::Term& term) {
  return term.kind == core::TermKind::Equality &&
         terms.get(term.children.at(0)).sort == terms.symbols().real_sort();
}

// Which bound on a prepared atom's tableau variable, and at what value, one
// truth assignment of that atom imposes (Section 3.1's infinitesimal-delta
// trick for strict inequalities).
struct BoundAssertion {
  bool is_upper;
  theory::arithmetic::DeltaRational value;
};

// `relation`/`rhs` describe the atom as `t ~ rhs`; `asserts_lower_when_true`
// says the atom was stored in the flipped orientation, i.e. it really means
// `t >= rhs` (or `t > rhs`), which arises when a single-variable atom's
// coefficient was negative and got divided out -- see PreparedLraTheory.
//
// All eight combinations, worked through explicitly because this is the one
// place a sign or strictness slip would silently change what the solver
// proves:
//
//   t <= c  TRUE -> upper = c        FALSE -> lower = c + delta
//   t <  c  TRUE -> upper = c - d    FALSE -> lower = c
//   t >= c  TRUE -> lower = c        FALSE -> upper = c - delta
//   t >  c  TRUE -> lower = c + d    FALSE -> upper = c
//
// which collapses to: negating an atom flips both its direction and its
// strictness, and the flipped orientation flips the direction once more.
//
// Shared between assert_lra_atom_bound (which applies this as an actual
// Assert-Bound call) and OnlineLraPropagator's propagation scan (which only
// compares against it, via LraTableau::upper_bound/lower_bound, to check
// whether some *other* atom on the same variable is already entailed).
// LraRelation::Eq isn't handled here -- it's unreachable via the frontend
// (Real equality is lowered to a pair of Le atoms by the
// lower_real_equalities preprocessing pass, which always runs before theory
// dispatch) and doesn't fit the single-bound shape these cases share.
BoundAssertion bound_assertion(theory::arithmetic::LraRelation relation, const util::Rational& rhs,
                               bool asserts_lower_when_true, bool atom_value) {
  const bool strict = relation == theory::arithmetic::LraRelation::Lt;
  const bool is_upper = asserts_lower_when_true ? !atom_value : atom_value;
  const bool is_strict = atom_value ? strict : !strict;
  if (!is_strict) {
    return BoundAssertion{is_upper, theory::arithmetic::DeltaRational::from_rational(rhs)};
  }
  // A strict upper bound is `t <= rhs - delta`; a strict lower one is
  // `t >= rhs + delta`.
  return BoundAssertion{is_upper,
                        theory::arithmetic::DeltaRational{rhs, util::Rational(is_upper ? -1 : 1)}};
}

// Whether `var`'s current bound in the direction `assertion` constrains is
// already at least as tight as `assertion` -- i.e. whether the atom that
// assertion came from is already entailed. Returns the entailing bound's
// reason literal alongside, for use as the propagation's antecedent.
std::optional<sat::Lit> bound_entails(const theory::arithmetic::LraTableau& tableau,
                                      theory::arithmetic::LraVarId var,
                                      const BoundAssertion& assertion) {
  if (assertion.is_upper) {
    const auto upper = tableau.upper_bound(var);
    if (upper.has_value() && upper->value <= assertion.value) {
      return upper->reason;
    }
    return std::nullopt;
  }
  const auto lower = tableau.lower_bound(var);
  if (lower.has_value() && lower->value >= assertion.value) {
    return lower->reason;
  }
  return std::nullopt;
}

// Defensive bounds on row-based/interval bound propagation's O(row size)
// per-affected-row cost -- unlike same-slack propagation (O(atoms sharing
// a slack), typically small), this is triggered by *any* bound change on
// a variable that occurs in potentially many rows. Not precisely derived;
// generous enough to matter for real formulas, small enough to bound
// worst-case cost on an adversarial/fuzzed input, tunable later against
// real evidence (mirrors the Gaussian-elimination caps' own precedent).
constexpr std::size_t kRowBasedPropagationMaxRowSize = 64;
constexpr std::size_t kRowBasedPropagationMaxFanout = 64;

// Competitiveness-roadmap item 7 (bidirectional row-based propagation):
// separate, smaller caps for the reverse (child-tightening) direction,
// since it does an O(row size) compute_implied_child_bound call for
// *each* other term of an affected row, not just once per row --
// multiplicatively pricier than item 2's own forward-only caps above.
// Not precisely derived either; deliberately smaller pending real
// evidence from this item's own quick-eval.
constexpr std::size_t kBidirectionalRowPropagationMaxRowSize = 32;
constexpr std::size_t kBidirectionalRowPropagationMaxFanout = 32;

// Applies one assigned LRA atom's relation+truth-value+strictness to
// `tableau` as a single Assert-Bound call (Eq asserts both bounds). Shared
// between the fully-lazy per-round-normalize path (not currently used by
// solve_qf_lra, kept for reference/tests) and the incremental
// prepared-atom path below.
std::optional<std::vector<sat::Lit>> assert_lra_atom_bound(
    theory::arithmetic::LraTableau& tableau, theory::arithmetic::LraVarId bound_var,
    theory::arithmetic::LraRelation relation, const util::Rational& rhs, bool atom_value,
    bool asserts_lower_when_true, sat::Lit reason) {
  switch (relation) {
    case theory::arithmetic::LraRelation::Eq: {
      if (!atom_value) {
        throw UnsupportedInput("QF_LRA disequality (a negated Real equality) is not yet supported");
      }
      // Equality is symmetric, so the flipped orientation makes no difference
      // here: both bounds land on the same value either way.
      auto conflict = tableau.assert_upper(
          bound_var, theory::arithmetic::DeltaRational::from_rational(rhs), reason);
      if (conflict.has_value()) {
        return conflict;
      }
      return tableau.assert_lower(bound_var, theory::arithmetic::DeltaRational::from_rational(rhs),
                                  reason);
    }
    case theory::arithmetic::LraRelation::Le:
    case theory::arithmetic::LraRelation::Lt: {
      const BoundAssertion assertion =
          bound_assertion(relation, rhs, asserts_lower_when_true, atom_value);
      return assertion.is_upper ? tableau.assert_upper(bound_var, assertion.value, reason)
                                : tableau.assert_lower(bound_var, assertion.value, reason);
    }
  }
  throw std::logic_error("unreachable: LraRelation switch is exhaustive");
}

// Canonical, order-independent identity for a LinearPolynomial's variable
// content (not its constant, which normalize_atom already moved to the
// atom's rhs): a sorted-by-TermId vector of (var, coefficient) pairs, used
// to let atoms that normalize to the same polynomial share a tableau slack
// variable/row (see PreparedLraTheory below).
//
// Scale-canonical (heuristics-note item H1b, the long-deferred roadmap item
// 3): every coefficient is divided by the coefficient of the lowest-indexed
// variable, so proportional polynomials collapse to one key -- `x + y <= 5`,
// `2x + 2y <= 10` and `-x - y <= -5` all key to `{x: 1, y: 1}` and share a
// slack, where before they got three separate ones and could not propagate
// against each other or see each other's bounds. Monniaux (CAV 2009,
// Section 2.1) describes the same normalization, "so that two inequalities
// constraining the same direction in space appear with the exact same
// left-hand side"; his form scales to coprime *integers*, ours to a leading
// coefficient of 1, which is the same equivalence class.
struct PolynomialKey {
  std::vector<std::pair<core::TermId, util::Rational>> sorted_terms;

  bool operator==(const PolynomialKey& other) const { return sorted_terms == other.sorted_terms; }
};

struct PolynomialKeyHash {
  std::size_t operator()(const PolynomialKey& key) const {
    std::size_t seed = key.sorted_terms.size();
    const util::RationalHash rational_hash;
    for (const auto& [var, coefficient] : key.sorted_terms) {
      seed ^= std::hash<core::TermId>{}(var) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
      seed ^= rational_hash(coefficient) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

// A polynomial's scale-canonical key together with the factor divided out.
// The caller needs the factor because dividing the polynomial has to be
// matched by dividing the atom's right-hand side, and a negative factor
// reverses the relation (which PreparedAtom::asserts_lower_when_true
// records).
struct CanonicalPolynomial {
  PolynomialKey key;
  util::Rational scale{1};
};

CanonicalPolynomial canonical_polynomial(const theory::arithmetic::LinearPolynomial& polynomial) {
  CanonicalPolynomial canonical;
  canonical.key.sorted_terms.assign(polynomial.terms().begin(), polynomial.terms().end());
  std::sort(canonical.key.sorted_terms.begin(), canonical.key.sorted_terms.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  if (canonical.key.sorted_terms.empty()) {
    return canonical;  // A constant polynomial has nothing to scale by.
  }
  canonical.scale = canonical.key.sorted_terms.front().second;
  if (canonical.scale == util::Rational(1)) {
    return canonical;  // Already canonical; skip the divisions entirely.
  }
  for (auto& [var, coefficient] : canonical.key.sorted_terms) {
    coefficient = coefficient / canonical.scale;
  }
  return canonical;
}

// Precomputes the QF_LRA tableau's structural part -- one row per distinct
// arithmetic-atom polynomial, shared problem variables -- exactly once,
// independent of any SAT assignment (Dutertre & de Moura Section 4).
// Mirrors PreparedFlatTheory's role for EUF: the per-round work becomes
// "copy this base and assert bounds", not "re-normalize and re-add rows
// from scratch every round" (what the fully-lazy path above still does).
//
// Atoms that normalize to the exact same LinearPolynomial (e.g. two `x<=...`
// atoms on the same variable, or the same compound term appearing in two
// different assertions) share a single slack variable/row rather than each
// getting its own -- this is what makes same-slack bound propagation
// (OnlineLraPropagator, below) find anything to propagate: propagation
// needs multiple atoms to share a tableau variable, and without this dedup
// they essentially never would.
struct PreparedLraTheory {
  // `sparse_leaving` is a search option rather than a structural one, but it
  // is configured here because the base tableau is what every solving path
  // copies from -- both OnlineLraPropagator and the non-IPASIR fallback.
  PreparedLraTheory(const core::TermManager& terms,
                    const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                    theory::arithmetic::LraTableau::PivotingRule pivoting_rule, bool sparse_leaving,
                    bool fixed_elimination, bool collect_model_variables);

  struct PreparedAtom {
    bool is_arithmetic = false;
    // The tableau variable this atom bounds: a slack for a compound
    // polynomial, or the problem variable itself for a single-variable atom
    // (see the constructor).
    theory::arithmetic::LraVarId bound_var = 0;
    theory::arithmetic::LraRelation relation = theory::arithmetic::LraRelation::Le;
    util::Rational rhs;
    // Set when dividing out a negative coefficient flipped the relation, so
    // the atom really means `bound_var >= rhs` rather than `<= rhs`. See
    // bound_assertion(), which is the single place this is interpreted.
    bool asserts_lower_when_true = false;
  };

  theory::arithmetic::LraTableau base_tableau;
  std::vector<PreparedAtom> atoms;  // parallel to theory_atoms
  // Indices (into `atoms`) of every arithmetic atom bounding a given tableau
  // variable -- built once here so OnlineLraPropagator's propagation scan
  // doesn't have to search linearly. Only populated for real tableau
  // variables (PreparedAtom::is_arithmetic); non-arithmetic atoms' default
  // bound_var (0) is a sentinel and must not be mixed in.
  std::unordered_map<theory::arithmetic::LraVarId, std::vector<std::size_t>> atoms_by_bound_var;
  std::unordered_map<core::TermId, theory::arithmetic::LraVarId> variables_by_term;
};

PreparedLraTheory::PreparedLraTheory(const core::TermManager& terms,
                                     const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                                     theory::arithmetic::LraTableau::PivotingRule pivoting_rule,
                                     bool sparse_leaving, bool fixed_elimination,
                                     bool collect_model_variables)
    : base_tableau(pivoting_rule) {
  base_tableau.set_sparse_leaving(sparse_leaving);
  std::unordered_map<core::TermId, theory::arithmetic::LraVarId> var_ids;
  auto get_var = [&](core::TermId term_id) {
    const auto found = var_ids.find(term_id);
    if (found != var_ids.end()) {
      return found->second;
    }
    const theory::arithmetic::LraVarId id = base_tableau.add_variable();
    var_ids.emplace(term_id, id);
    if (collect_model_variables) {
      variables_by_term.emplace(term_id, id);
    }
    return id;
  };

  std::unordered_map<PolynomialKey, theory::arithmetic::LraVarId, PolynomialKeyHash>
      slack_by_polynomial;

  atoms.reserve(theory_atoms.size());
  for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
    const core::Term& term = terms.get(atom.term);
    const bool is_comparison = term.kind == core::TermKind::Le || term.kind == core::TermKind::Lt;
    if (!is_comparison && !is_real_equality_term(terms, term)) {
      atoms.push_back(PreparedAtom{});
      continue;
    }

    const theory::arithmetic::LraAtom normalized =
        theory::arithmetic::normalize_atom(terms, atom.term);
    // Scale-canonicalize first (item H1b), then divide the atom's right-hand
    // side by the same factor. A negative factor reverses the relation, which
    // is what asserts_lower_when_true records -- interpreted in exactly one
    // place, bound_assertion().
    const CanonicalPolynomial canonical = canonical_polynomial(normalized.polynomial);
    const util::Rational rhs =
        canonical.scale == util::Rational(1) ? normalized.rhs : normalized.rhs / canonical.scale;
    const bool flipped = canonical.scale.sign() < 0;

    // A single-variable atom is a bound on that variable itself: no slack and
    // no row at all (item H1a). Monniaux (CAV 2009, Section 2.1) states this
    // as ordinary practice -- "for each left hand side that is not a variable,
    // a new variable is introduced" -- and Dutertre & de Moura's Section 2
    // does the same; Primo previously introduced a slack for *every* distinct
    // polynomial, which on the lagging families meant 36-95% of all tableau
    // rows were trivial `s = c*x` rows (measurements in
    // docs/qf-lra-heuristics.md, item H1). After canonicalization the single
    // remaining coefficient is exactly 1, so the variable is the polynomial.
    //
    // Together with canonicalization this puts one variable's lower and upper
    // bound on the same tableau variable: `(<= 0 x)` normalizes to a
    // coefficient of -1 and `(<= x 10)` to +1, so under the old exact-content
    // key they landed on two *different* slacks and the tableau could not see
    // that `x` was bounded on both sides without pivoting.
    if (canonical.key.sorted_terms.size() == 1) {
      const theory::arithmetic::LraVarId bound_var = get_var(canonical.key.sorted_terms[0].first);
      const std::size_t atom_index = atoms.size();
      atoms.push_back(PreparedAtom{true, bound_var, normalized.relation, rhs, flipped});
      atoms_by_bound_var[bound_var].push_back(atom_index);
      continue;
    }

    const auto existing_slack = slack_by_polynomial.find(canonical.key);
    theory::arithmetic::LraVarId slack;
    if (existing_slack != slack_by_polynomial.end()) {
      slack = existing_slack->second;
    } else {
      std::vector<std::pair<theory::arithmetic::LraVarId, util::Rational>> row;
      row.reserve(canonical.key.sorted_terms.size());
      for (const auto& [var_term, coefficient] : canonical.key.sorted_terms) {
        row.emplace_back(get_var(var_term), coefficient);
      }
      slack = base_tableau.add_variable();
      base_tableau.add_row(slack, row);
      slack_by_polynomial.emplace(canonical.key, slack);
    }
    const std::size_t atom_index = atoms.size();
    atoms.push_back(PreparedAtom{true, slack, normalized.relation, rhs, flipped});
    atoms_by_bound_var[slack].push_back(atom_index);
  }

  base_tableau.set_fixed_elimination(fixed_elimination);
}

std::optional<util::Rational> positive_delta_upper_bound(theory::arithmetic::DeltaRational lhs,
                                                         theory::arithmetic::DeltaRational rhs) {
  const util::Rational rational_gap = rhs.rational - lhs.rational;
  const util::Rational infinitesimal_diff = lhs.infinitesimal - rhs.infinitesimal;
  if (rational_gap.sign() > 0 && infinitesimal_diff.sign() > 0) {
    return rational_gap / infinitesimal_diff;
  }
  return std::nullopt;
}

void tighten_delta_upper_bound(std::optional<util::Rational>& upper,
                               const std::optional<util::Rational>& candidate) {
  if (!candidate.has_value() || candidate->sign() <= 0) {
    return;
  }
  if (!upper.has_value() || candidate.value() < upper.value()) {
    upper = candidate;
  }
}

util::Rational choose_concrete_delta(const PreparedLraTheory& prepared,
                                     const theory::arithmetic::LraTableau& tableau,
                                     const std::vector<AssignedTheoryAtom>& assigned_atoms) {
  std::optional<util::Rational> upper;
  for (std::size_t i = 0; i < assigned_atoms.size(); ++i) {
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms.at(i);
    if (!prepared_atom.is_arithmetic || assigned_atoms.at(i).filtered) {
      continue;
    }
    const theory::arithmetic::DeltaRational value = tableau.assignment(prepared_atom.bound_var);
    if (prepared_atom.relation == theory::arithmetic::LraRelation::Eq) {
      continue;
    }
    const BoundAssertion assertion =
        bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                        prepared_atom.asserts_lower_when_true, assigned_atoms.at(i).value);
    if (assertion.is_upper) {
      tighten_delta_upper_bound(upper, positive_delta_upper_bound(value, assertion.value));
    } else {
      tighten_delta_upper_bound(upper, positive_delta_upper_bound(assertion.value, value));
    }
  }
  return upper.has_value() ? upper.value() / util::Rational(2) : util::Rational(1);
}

util::Rational concretize_delta_rational(theory::arithmetic::DeltaRational value,
                                         const util::Rational& delta) {
  return value.rational + value.infinitesimal * delta;
}

using RealModelValues = std::unordered_map<core::TermId, util::Rational>;

std::optional<util::Rational> evaluate_real_model_term(const core::TermManager& terms,
                                                       core::TermId term_id,
                                                       const RealModelValues& values) {
  const core::Term& term = terms.get(term_id);
  if (term.sort != terms.symbols().real_sort()) {
    return std::nullopt;
  }
  switch (term.kind) {
    case core::TermKind::Rational:
      return term.rational_value;
    case core::TermKind::Constant: {
      const auto found = values.find(term_id);
      return found == values.end() ? std::nullopt : std::optional<util::Rational>{found->second};
    }
    case core::TermKind::Plus: {
      util::Rational sum(0);
      for (const core::TermId child : term.children) {
        const std::optional<util::Rational> child_value =
            evaluate_real_model_term(terms, child, values);
        if (!child_value.has_value()) {
          return std::nullopt;
        }
        sum = sum + child_value.value();
      }
      return sum;
    }
    case core::TermKind::Times: {
      const std::optional<util::Rational> child_value =
          evaluate_real_model_term(terms, term.children.at(0), values);
      if (!child_value.has_value()) {
        return std::nullopt;
      }
      return term.rational_value * child_value.value();
    }
    default:
      return std::nullopt;
  }
}

bool assign_real_model_value(const core::TermManager& terms, RealModelValues& values,
                             core::TermId target, const util::Rational& value) {
  const core::Term& term = terms.get(target);
  if (term.kind != core::TermKind::Constant || term.sort != terms.symbols().real_sort()) {
    return false;
  }
  const auto [it, inserted] = values.emplace(target, value);
  if (!inserted && it->second != value) {
    it->second = value;
    return true;
  }
  return inserted;
}

void apply_real_preprocessing_model_assignments(
    const core::TermManager& terms, RealModelValues& values,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (const TheoryModelAssignment& assignment : preprocessing_model_assignments) {
      if (assignment.filtered || !assignment.value) {
        continue;
      }
      const core::Term& equality = terms.get(assignment.term);
      if (equality.kind != core::TermKind::Equality || equality.children.size() != 2) {
        continue;
      }
      const core::TermId lhs = equality.children.at(0);
      const core::TermId rhs = equality.children.at(1);
      if (terms.get(lhs).sort != terms.symbols().real_sort() ||
          terms.get(rhs).sort != terms.symbols().real_sort()) {
        continue;
      }
      const std::optional<util::Rational> lhs_value = evaluate_real_model_term(terms, lhs, values);
      const std::optional<util::Rational> rhs_value = evaluate_real_model_term(terms, rhs, values);
      if (rhs_value.has_value()) {
        changed |= assign_real_model_value(terms, values, lhs, rhs_value.value());
      }
      if (lhs_value.has_value()) {
        changed |= assign_real_model_value(terms, values, rhs, lhs_value.value());
      }
    }
  }
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

void append_real_constant_model_definitions(std::ostringstream& model,
                                            const core::TermManager& terms,
                                            const std::vector<core::TermId>& model_terms,
                                            const RealModelValues& values) {
  for (const core::TermId term_id : model_terms) {
    const core::Term& term = terms.get(term_id);
    if (term.kind != core::TermKind::Constant || term.sort != terms.symbols().real_sort()) {
      continue;
    }
    const core::Symbol& symbol = terms.symbols().get_symbol(term.symbol);
    const auto found = values.find(term_id);
    const util::Rational value = found == values.end() ? util::Rational(0) : found->second;
    model << "  (define-fun " << symbol.name << " () Real " << smtlib_rational(value) << ")\n";
  }
}

void append_supported_uf_model_assignments(const core::TermManager& terms,
                                           std::vector<TheoryModelAssignment>& target,
                                           const std::vector<TheoryModelAssignment>& assignments);

std::string build_composed_uf_lra_model(
    core::TermManager& terms, const std::vector<core::TermId>& model_terms,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments,
    const RealModelValues& values) {
  std::vector<TheoryModelAssignment> uf_model_assignments;
  append_supported_uf_model_assignments(terms, uf_model_assignments,
                                        preprocessing_model_assignments);

  std::ostringstream model;
  model << "(model\n";
  model << build_qf_uf_model(terms, uf_model_assignments,
                             QfUfModelBuildOptions{.include_model_wrapper = false,
                                                   .skip_real_symbols = false,
                                                   .skip_real_constants = true,
                                                   .real_values = &values});
  append_real_constant_model_definitions(model, terms, model_terms, values);
  model << ")";
  return model.str();
}

RealModelValues real_model_values_from_lra(
    const core::TermManager& terms, const std::vector<core::TermId>& model_terms,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments,
    const PreparedLraTheory& prepared, const theory::arithmetic::LraTableau& tableau,
    const std::vector<AssignedTheoryAtom>& assigned_atoms) {
  const util::Rational concrete_delta = choose_concrete_delta(prepared, tableau, assigned_atoms);
  RealModelValues values;
  values.reserve(prepared.variables_by_term.size() + model_terms.size());
  for (const auto& [term_id, variable] : prepared.variables_by_term) {
    values.emplace(term_id,
                   concretize_delta_rational(tableau.assignment(variable), concrete_delta));
  }
  apply_real_preprocessing_model_assignments(terms, values, preprocessing_model_assignments);
  return values;
}

std::string build_qf_lra_model(
    core::TermManager& terms, const std::vector<core::TermId>& model_terms,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments,
    const PreparedLraTheory& prepared, const theory::arithmetic::LraTableau& tableau,
    const std::vector<AssignedTheoryAtom>& assigned_atoms) {
  const RealModelValues values = real_model_values_from_lra(
      terms, model_terms, preprocessing_model_assignments, prepared, tableau, assigned_atoms);
  return build_composed_uf_lra_model(terms, model_terms, preprocessing_model_assignments, values);
}

std::string build_qf_lra_preprocessing_only_model(
    core::TermManager& terms, const std::vector<core::TermId>& model_terms,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments) {
  RealModelValues values;
  values.reserve(model_terms.size());
  apply_real_preprocessing_model_assignments(terms, values, preprocessing_model_assignments);
  return build_composed_uf_lra_model(terms, model_terms, preprocessing_model_assignments, values);
}

std::vector<AssignedTheoryAtom> assigned_atoms_from_backend(
    const sat::IpasirUpBackend& backend,
    const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms) {
  std::vector<AssignedTheoryAtom> assigned_atoms;
  assigned_atoms.reserve(theory_atoms.size());
  for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
    const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    const std::optional<bool> assignment = backend.value(var);
    if (!assignment.has_value()) {
      throw UnsupportedInput("SAT backend did not assign theory atom");
    }
    const bool atom_value = atom.lit.value > 0 ? assignment.value() : !assignment.value();
    assigned_atoms.push_back(assigned_theory_atom(PureTheoryFilterMode::Off, atom, atom_value));
  }
  return assigned_atoms;
}

std::vector<AssignedTheoryAtom> assigned_atoms_from_backend(
    const sat::IpasirUpBackend& backend,
    const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms, PureTheoryFilterMode mode) {
  std::vector<AssignedTheoryAtom> assigned_atoms;
  assigned_atoms.reserve(theory_atoms.size());
  for (const BooleanClausifier::TheoryAtom& atom : theory_atoms) {
    const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    const std::optional<bool> assignment = backend.value(var);
    if (!assignment.has_value()) {
      throw UnsupportedInput("SAT backend did not assign theory atom");
    }
    const bool atom_value = atom.lit.value > 0 ? assignment.value() : !assignment.value();
    assigned_atoms.push_back(assigned_theory_atom(mode, atom, atom_value));
  }
  return assigned_atoms;
}

bool is_lra_theory_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Le || term.kind == core::TermKind::Lt) {
    return true;
  }
  return term.kind == core::TermKind::Equality &&
         terms.get(term.children.at(0)).sort == terms.symbols().real_sort();
}

bool is_real_euf_definition_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  return term.kind == core::TermKind::Equality &&
         terms.get(term.children.at(0)).sort == terms.symbols().real_sort() &&
         contains_application_term(terms, term_id);
}

bool is_euf_theory_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Equality) {
    if (terms.get(term.children.at(0)).sort == terms.symbols().real_sort()) {
      return contains_application_term(terms, term_id);
    }
    return terms.get(term.children.at(0)).sort != terms.symbols().real_sort();
  }
  return (term.kind == core::TermKind::Application || term.kind == core::TermKind::Constant) &&
         term.sort == terms.symbols().bool_sort();
}

bool contains_real_sorted_term(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.sort == terms.symbols().real_sort()) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_real_sorted_term(terms, child)) {
      return true;
    }
  }
  return false;
}

bool contains_nonconstant_real_sorted_term(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.sort == terms.symbols().real_sort() && term.kind != core::TermKind::Constant) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_nonconstant_real_sorted_term(terms, child)) {
      return true;
    }
  }
  return false;
}

void require_supported_mixed_theory_atom(const core::TermManager& terms,
                                         const BooleanClausifier::TheoryAtom& atom) {
  if (is_euf_theory_atom(terms, atom.term)) {
    if (contains_real_sorted_term(terms, atom.term) &&
        !is_real_euf_definition_atom(terms, atom.term) &&
        contains_nonconstant_real_sorted_term(terms, atom.term)) {
      throw UnsupportedInput(
          "QF_UFLRA with EUF atoms over non-constant Real terms needs purification");
    }
    return;
  }
  if (is_lra_theory_atom(terms, atom.term)) {
    if (contains_application_term(terms, atom.term)) {
      throw UnsupportedInput(
          "QF_UFLRA arithmetic atom escaped Real-valued UF-application purification");
    }
    return;
  }
  throw UnsupportedInput("unsupported mixed-theory atom");
}

void collect_real_constants_in_term(const core::TermManager& terms, core::TermId term_id,
                                    std::unordered_set<core::TermId>& out) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Constant && term.sort == terms.symbols().real_sort()) {
    out.insert(term_id);
  }
  for (const core::TermId child : term.children) {
    collect_real_constants_in_term(terms, child, out);
  }
}

std::vector<core::TermId> shared_real_constants(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& euf_atoms,
    const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms) {
  std::unordered_set<core::TermId> euf_terms;
  std::unordered_set<core::TermId> lra_terms;
  for (const BooleanClausifier::TheoryAtom& atom : euf_atoms) {
    collect_real_constants_in_term(terms, atom.term, euf_terms);
  }
  for (const BooleanClausifier::TheoryAtom& atom : lra_atoms) {
    collect_real_constants_in_term(terms, atom.term, lra_terms);
  }

  std::vector<core::TermId> shared;
  shared.reserve(std::min(euf_terms.size(), lra_terms.size()));
  for (const core::TermId term : euf_terms) {
    if (lra_terms.contains(term)) {
      shared.push_back(term);
    }
  }
  std::sort(shared.begin(), shared.end());
  return shared;
}

std::vector<TermPairKey> all_term_pairs(const std::vector<core::TermId>& terms) {
  std::vector<TermPairKey> pairs;
  pairs.reserve(unordered_pair_count(terms.size()));
  for (std::size_t i = 0; i < terms.size(); ++i) {
    for (std::size_t j = i + 1; j < terms.size(); ++j) {
      pairs.push_back(canonical_term_pair(terms[i], terms[j]));
    }
  }
  return pairs;
}

std::vector<core::TermId> real_constants_in_atoms(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& atoms) {
  std::unordered_set<core::TermId> real_terms;
  for (const BooleanClausifier::TheoryAtom& atom : atoms) {
    collect_real_constants_in_term(terms, atom.term, real_terms);
  }
  std::vector<core::TermId> result(real_terms.begin(), real_terms.end());
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<core::TermId> euf_implied_real_equalities(
    core::TermManager& terms, const PreparedFlatTheory& prepared,
    theory::equality::FlatCongruenceClosure& closure,
    const std::vector<core::TermId>& shared_terms) {
  std::vector<core::TermId> equalities;
  for (std::size_t i = 0; i < shared_terms.size(); ++i) {
    const std::optional<theory::equality::FlatTermId> lhs =
        prepared.flat.existing_flat_term(shared_terms[i]);
    if (!lhs.has_value()) {
      continue;
    }
    for (std::size_t j = i + 1; j < shared_terms.size(); ++j) {
      const std::optional<theory::equality::FlatTermId> rhs =
          prepared.flat.existing_flat_term(shared_terms[j]);
      if (!rhs.has_value() || !closure.are_equal(lhs.value(), rhs.value())) {
        continue;
      }
      const core::TermId equality = terms.mk_eq(shared_terms[i], shared_terms[j]);
      if (terms.get(equality).kind == core::TermKind::Equality) {
        equalities.push_back(equality);
      }
    }
  }
  return equalities;
}

std::optional<sat::Clause> find_incremental_lra_theory_conflict(
    const PreparedLraTheory& prepared, const std::vector<AssignedTheoryAtom>& atoms);

bool lra_assignment_conflicts_with_extra_atom(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments, core::TermId extra_atom,
    const CheckSatOptions& options) {
  std::vector<BooleanClausifier::TheoryAtom> augmented_lra_atoms = lra_atoms;
  augmented_lra_atoms.push_back(BooleanClausifier::TheoryAtom{sat::Lit{0}, extra_atom});
  std::vector<AssignedTheoryAtom> augmented_lra_assignments = lra_assignments;
  augmented_lra_assignments.push_back(AssignedTheoryAtom{0, true, extra_atom, false});
  const PreparedLraTheory augmented_lra_theory(
      terms, augmented_lra_atoms, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
      options.lra_sparse_leaving, options.lra_fixed_elimination, false);
  return find_incremental_lra_theory_conflict(augmented_lra_theory, augmented_lra_assignments)
      .has_value();
}

std::vector<core::TermId> lra_implied_real_equalities(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<core::TermId>& shared_terms, const CheckSatOptions& options) {
  std::vector<core::TermId> equalities;
  for (std::size_t i = 0; i < shared_terms.size(); ++i) {
    for (std::size_t j = i + 1; j < shared_terms.size(); ++j) {
      const core::TermId lhs = shared_terms[i];
      const core::TermId rhs = shared_terms[j];
      const bool lhs_lt_rhs_conflicts = lra_assignment_conflicts_with_extra_atom(
          terms, lra_atoms, lra_assignments, terms.mk_lt(lhs, rhs), options);
      if (!lhs_lt_rhs_conflicts) {
        continue;
      }
      const bool rhs_lt_lhs_conflicts = lra_assignment_conflicts_with_extra_atom(
          terms, lra_atoms, lra_assignments, terms.mk_lt(rhs, lhs), options);
      if (rhs_lt_lhs_conflicts) {
        equalities.push_back(terms.mk_eq(lhs, rhs));
      }
    }
  }
  return equalities;
}

bool lra_assignment_conflicts_with_extra_atoms(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<core::TermId>& extra_atoms, const CheckSatOptions& options) {
  std::vector<BooleanClausifier::TheoryAtom> augmented_lra_atoms = lra_atoms;
  augmented_lra_atoms.reserve(lra_atoms.size() + extra_atoms.size());
  std::vector<AssignedTheoryAtom> augmented_lra_assignments = lra_assignments;
  augmented_lra_assignments.reserve(lra_assignments.size() + extra_atoms.size());
  for (const core::TermId extra_atom : extra_atoms) {
    augmented_lra_atoms.push_back(BooleanClausifier::TheoryAtom{sat::Lit{0}, extra_atom});
    augmented_lra_assignments.push_back(AssignedTheoryAtom{0, true, extra_atom, false});
  }
  const PreparedLraTheory augmented_lra_theory(
      terms, augmented_lra_atoms, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
      options.lra_sparse_leaving, options.lra_fixed_elimination, false);
  return find_incremental_lra_theory_conflict(augmented_lra_theory, augmented_lra_assignments)
      .has_value();
}

bool flat_closure_equal_on_terms(const PreparedFlatTheory& prepared,
                                 theory::equality::FlatCongruenceClosure& closure, core::TermId lhs,
                                 core::TermId rhs) {
  const std::optional<theory::equality::FlatTermId> lhs_flat =
      prepared.flat.existing_flat_term(lhs);
  const std::optional<theory::equality::FlatTermId> rhs_flat =
      prepared.flat.existing_flat_term(rhs);
  return lhs_flat.has_value() && rhs_flat.has_value() &&
         closure.are_equal(lhs_flat.value(), rhs_flat.value());
}

bool equality_list_contains_pair(const core::TermManager& terms,
                                 const std::vector<core::TermId>& equalities, core::TermId lhs,
                                 core::TermId rhs) {
  for (const core::TermId equality_id : equalities) {
    const core::Term& equality = terms.get(equality_id);
    if (equality.kind != core::TermKind::Equality || equality.children.size() != 2) {
      continue;
    }
    const core::TermId current_lhs = equality.children.at(0);
    const core::TermId current_rhs = equality.children.at(1);
    if ((current_lhs == lhs && current_rhs == rhs) || (current_lhs == rhs && current_rhs == lhs)) {
      return true;
    }
  }
  return false;
}

struct RealSeparationCandidate {
  core::TermId lhs;
  core::TermId rhs;
};

constexpr std::size_t kNoModelSeparationBacktrackingCandidateLimit = 18;

bool search_shared_real_model_separations(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<RealSeparationCandidate>& candidates, std::size_t next_candidate,
    std::vector<core::TermId>& accumulated, std::vector<core::TermId>& separations,
    const CheckSatOptions& options, std::uint64_t& branches, std::uint64_t& backtracks) {
  if (next_candidate == candidates.size()) {
    return true;
  }

  const RealSeparationCandidate& candidate = candidates[next_candidate];
  const core::TermId lhs_lt_rhs = terms.mk_lt(candidate.lhs, candidate.rhs);
  const core::TermId rhs_lt_lhs = terms.mk_lt(candidate.rhs, candidate.lhs);

  const auto try_orientation = [&](core::TermId atom) {
    ++branches;
    accumulated.push_back(atom);
    if (!lra_assignment_conflicts_with_extra_atoms(terms, lra_atoms, lra_assignments, accumulated,
                                                   options)) {
      separations.push_back(atom);
      if (search_shared_real_model_separations(terms, lra_atoms, lra_assignments, candidates,
                                               next_candidate + 1, accumulated, separations,
                                               options, branches, backtracks)) {
        return true;
      }
      separations.pop_back();
    }
    accumulated.pop_back();
    return false;
  };

  if (try_orientation(lhs_lt_rhs) || try_orientation(rhs_lt_lhs)) {
    return true;
  }

  ++backtracks;
  return false;
}

std::optional<std::vector<core::TermId>> greedy_shared_real_model_separation_atoms(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<RealSeparationCandidate>& candidates,
    const std::vector<core::TermId>& forced_equalities, const CheckSatOptions& options) {
  std::vector<core::TermId> accumulated = forced_equalities;
  std::vector<core::TermId> separations;
  for (const RealSeparationCandidate& candidate : candidates) {
    const core::TermId lhs_lt_rhs = terms.mk_lt(candidate.lhs, candidate.rhs);
    accumulated.push_back(lhs_lt_rhs);
    if (!lra_assignment_conflicts_with_extra_atoms(terms, lra_atoms, lra_assignments, accumulated,
                                                   options)) {
      separations.push_back(lhs_lt_rhs);
      continue;
    }
    accumulated.pop_back();

    const core::TermId rhs_lt_lhs = terms.mk_lt(candidate.rhs, candidate.lhs);
    accumulated.push_back(rhs_lt_lhs);
    if (!lra_assignment_conflicts_with_extra_atoms(terms, lra_atoms, lra_assignments, accumulated,
                                                   options)) {
      separations.push_back(rhs_lt_lhs);
      continue;
    }
    return std::nullopt;
  }
  return separations;
}

std::optional<std::vector<core::TermId>> shared_real_model_separation_atoms(
    core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments, const PreparedFlatTheory& prepared_euf,
    theory::equality::FlatCongruenceClosure& closure, const std::vector<core::TermId>& shared_terms,
    const std::vector<core::TermId>& forced_equalities, const CheckSatOptions& options,
    util::Statistics& statistics) {
  std::vector<RealSeparationCandidate> candidates;
  std::vector<core::TermId> accumulated = forced_equalities;
  for (std::size_t i = 0; i < shared_terms.size(); ++i) {
    for (std::size_t j = i + 1; j < shared_terms.size(); ++j) {
      const core::TermId lhs = shared_terms[i];
      const core::TermId rhs = shared_terms[j];
      if (flat_closure_equal_on_terms(prepared_euf, closure, lhs, rhs) ||
          equality_list_contains_pair(terms, forced_equalities, lhs, rhs)) {
        continue;
      }
      candidates.push_back(RealSeparationCandidate{lhs, rhs});
    }
  }

  statistics.set(":no.interface.model-separation-candidates", candidates.size());
  statistics.set(":no.interface.model-separation-search-capped", 0);
  statistics.set(":no.interface.model-separation-search-branches", 0);
  statistics.set(":no.interface.model-separation-search-backtracks", 0);
  if (candidates.size() > kNoModelSeparationBacktrackingCandidateLimit) {
    statistics.set(":no.interface.model-separation-search-capped", 1);
    return greedy_shared_real_model_separation_atoms(terms, lra_atoms, lra_assignments, candidates,
                                                     forced_equalities, options);
  }

  std::vector<core::TermId> separations;
  std::uint64_t branches = 0;
  std::uint64_t backtracks = 0;
  if (!search_shared_real_model_separations(terms, lra_atoms, lra_assignments, candidates, 0,
                                            accumulated, separations, options, branches,
                                            backtracks)) {
    statistics.set(":no.interface.model-separation-search-branches", branches);
    statistics.set(":no.interface.model-separation-search-backtracks", backtracks);
    return std::nullopt;
  }
  statistics.set(":no.interface.model-separation-search-branches", branches);
  statistics.set(":no.interface.model-separation-search-backtracks", backtracks);
  return separations;
}

void append_supported_uf_model_assignments(const core::TermManager& terms,
                                           std::vector<TheoryModelAssignment>& target,
                                           const std::vector<TheoryModelAssignment>& assignments) {
  target.reserve(target.size() + assignments.size());
  for (const TheoryModelAssignment& assignment : assignments) {
    if (!contains_real_sorted_term(terms, assignment.term)) {
      target.push_back(assignment);
    }
  }
}

struct LraModelState {
  PreparedLraTheory prepared;
  theory::arithmetic::LraTableau tableau;
  std::vector<AssignedTheoryAtom> assignments;
};

std::optional<LraModelState> build_lra_model_state(
    const core::TermManager& terms, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<core::TermId>& extra_true_atoms, const CheckSatOptions& options) {
  std::vector<BooleanClausifier::TheoryAtom> augmented_lra_atoms = lra_atoms;
  augmented_lra_atoms.reserve(lra_atoms.size() + extra_true_atoms.size());
  std::vector<AssignedTheoryAtom> augmented_lra_assignments = lra_assignments;
  augmented_lra_assignments.reserve(lra_assignments.size() + extra_true_atoms.size());
  for (const core::TermId atom : extra_true_atoms) {
    augmented_lra_atoms.push_back(BooleanClausifier::TheoryAtom{sat::Lit{0}, atom});
    augmented_lra_assignments.push_back(AssignedTheoryAtom{0, true, atom, false});
  }

  PreparedLraTheory prepared(terms, augmented_lra_atoms,
                             lra_pivoting_rule_from_options(options.lra_pivoting_rule),
                             options.lra_sparse_leaving, options.lra_fixed_elimination, true);
  theory::arithmetic::LraTableau tableau = prepared.base_tableau;
  for (std::size_t i = 0; i < augmented_lra_assignments.size(); ++i) {
    const AssignedTheoryAtom& atom = augmented_lra_assignments[i];
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms[i];
    if (!prepared_atom.is_arithmetic || atom.filtered) {
      continue;
    }
    const std::optional<sat::Clause> conflict = assert_lra_atom_bound(
        tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom.value,
        prepared_atom.asserts_lower_when_true, blocking_lit(atom));
    if (conflict.has_value()) {
      return std::nullopt;
    }
  }
  if (tableau.check().has_value()) {
    return std::nullopt;
  }
  return LraModelState{std::move(prepared), std::move(tableau),
                       std::move(augmented_lra_assignments)};
}

std::optional<sat::Lit> choose_lra_model_equality_branch(
    core::TermManager& terms, BooleanClausifier& clausifier, sat::CadicalBackend& backend,
    TheoryCombinationManager& manager, const std::vector<BooleanClausifier::TheoryAtom>& lra_atoms,
    const std::vector<AssignedTheoryAtom>& lra_assignments,
    const std::vector<TermPairKey>& candidate_pairs, const CheckSatOptions& options,
    std::unordered_set<int>& tried_model_equality_vars, util::Statistics& statistics) {
  if (!options.nelson_oppen_model_equality_branching || candidate_pairs.empty() ||
      tried_model_equality_vars.size() >= options.nelson_oppen_model_equality_branch_budget) {
    return std::nullopt;
  }

  std::optional<LraModelState> model_state =
      build_lra_model_state(terms, lra_atoms, lra_assignments, {}, options);
  if (!model_state.has_value()) {
    return std::nullopt;
  }
  std::vector<core::TermId> care_terms;
  care_terms.reserve(candidate_pairs.size() * 2U);
  for (const TermPairKey& pair : candidate_pairs) {
    care_terms.push_back(pair.lhs);
    care_terms.push_back(pair.rhs);
  }
  std::sort(care_terms.begin(), care_terms.end());
  care_terms.erase(std::unique(care_terms.begin(), care_terms.end()), care_terms.end());
  const RealModelValues real_values = real_model_values_from_lra(
      terms, care_terms, {}, model_state->prepared, model_state->tableau, model_state->assignments);
  const auto value_of = [&](core::TermId term) {
    const auto found = real_values.find(term);
    return found == real_values.end() ? util::Rational(0) : found->second;
  };

  statistics.set(":no.interface.model-equality-full-pairs", candidate_pairs.size());
  statistics.set(":no.interface.model-equality-care-pairs", candidate_pairs.size());
  statistics.set(":no.interface.model-equality-care-pruned-pairs", 0);
  for (const TermPairKey& pair : candidate_pairs) {
    const core::TermId lhs = pair.lhs;
    const core::TermId rhs = pair.rhs;
    if (value_of(lhs) != value_of(rhs)) {
      continue;
    }
    statistics.increment(":no.interface.model-equality-candidates");
    const core::TermId equality = terms.mk_eq(lhs, rhs);
    if (theory_atom_occurs_in_formula(clausifier, equality)) {
      continue;
    }

    std::optional<sat::Lit> lit = manager.interface_equality_lit(equality);
    bool created = false;
    if (!lit.has_value()) {
      lit = create_interface_equality_lit(terms, clausifier, manager, equality);
      created = lit.has_value();
    }
    if (!lit.has_value()) {
      continue;
    }
    const int var = lit->value > 0 ? lit->value : -lit->value;
    if (tried_model_equality_vars.contains(var)) {
      continue;
    }
    if (!created) {
      const std::optional<bool> value = lit_value_in_backend(backend, lit.value());
      if (value.has_value()) {
        continue;
      }
    }
    tried_model_equality_vars.insert(var);
    backend.phase(lit.value());
    statistics.increment(":no.interface.model-equality-created", created ? 1 : 0);
    statistics.set(":no.interface.sat-equality-atoms",
                   registered_interface_equality_atom_count(manager));
    statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
    return lit;
  }
  return std::nullopt;
}

struct MixedTheoryPurificationResult {
  std::vector<core::TermId> assertions;
  std::size_t arithmetic_uf_definitions = 0;
  std::size_t euf_real_term_definitions = 0;
  std::size_t reused_applications = 0;
  std::size_t reused_real_terms = 0;
};

core::TermId purify_mixed_formula(
    Frontend& frontend, core::TermId term_id,
    std::unordered_map<core::TermId, core::TermId>& proxy_by_app,
    std::vector<core::TermId>& app_definitions, std::size_t& reused_applications,
    std::unordered_map<core::TermId, core::TermId>& proxy_by_real_term,
    std::vector<core::TermId>& lra_definition_atoms, std::size_t& reused_real_terms);

core::TermId purify_euf_term(Frontend& frontend, core::TermId term_id,
                             std::unordered_map<core::TermId, core::TermId>& proxy_by_app,
                             std::vector<core::TermId>& app_definitions,
                             std::size_t& reused_applications,
                             std::unordered_map<core::TermId, core::TermId>& proxy_by_real_term,
                             std::vector<core::TermId>& lra_definition_atoms,
                             std::size_t& reused_real_terms);

core::TermId purify_arithmetic_term(
    Frontend& frontend, core::TermId term_id,
    std::unordered_map<core::TermId, core::TermId>& proxy_by_app,
    std::vector<core::TermId>& app_definitions, std::size_t& reused_applications,
    std::unordered_map<core::TermId, core::TermId>& proxy_by_real_term,
    std::vector<core::TermId>& lra_definition_atoms, std::size_t& reused_real_terms) {
  core::TermManager& terms = frontend.terms();
  const core::Term& term = terms.get(term_id);
  const core::TermKind kind = term.kind;
  const core::SortId sort = term.sort;
  const core::SymbolId symbol = term.symbol;
  const std::vector<core::TermId> children = term.children;
  const util::Rational rational_value = term.rational_value;
  if (kind == core::TermKind::Application && sort == terms.symbols().real_sort()) {
    const auto found = proxy_by_app.find(term_id);
    if (found != proxy_by_app.end()) {
      ++reused_applications;
      return found->second;
    }
    const core::TermId proxy = frontend.fresh_internal_constant(sort, "no_proxy");
    proxy_by_app.emplace(term_id, proxy);
    std::vector<core::TermId> purified_children;
    purified_children.reserve(children.size());
    bool changed = false;
    for (const core::TermId child : children) {
      const core::TermId purified =
          purify_euf_term(frontend, child, proxy_by_app, app_definitions, reused_applications,
                          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      changed = changed || purified != child;
      purified_children.push_back(purified);
    }
    const core::TermId purified_application =
        changed ? terms.mk_app(symbol, std::move(purified_children)) : term_id;
    app_definitions.push_back(terms.mk_eq(proxy, purified_application));
    return proxy;
  }

  switch (kind) {
    case core::TermKind::Plus: {
      std::vector<core::TermId> rewritten_children;
      rewritten_children.reserve(children.size());
      bool changed = false;
      for (const core::TermId child : children) {
        const core::TermId rewritten = purify_arithmetic_term(
            frontend, child, proxy_by_app, app_definitions, reused_applications, proxy_by_real_term,
            lra_definition_atoms, reused_real_terms);
        changed = changed || rewritten != child;
        rewritten_children.push_back(rewritten);
      }
      return changed ? terms.mk_plus(std::move(rewritten_children)) : term_id;
    }
    case core::TermKind::Times: {
      const core::TermId child = purify_arithmetic_term(
          frontend, children.at(0), proxy_by_app, app_definitions, reused_applications,
          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      return child == children.at(0) ? term_id : terms.mk_times(rational_value, child);
    }
    default:
      return term_id;
  }
}

core::TermId purify_euf_real_argument(
    Frontend& frontend, core::TermId term_id,
    std::unordered_map<core::TermId, core::TermId>& proxy_by_app,
    std::vector<core::TermId>& app_definitions, std::size_t& reused_applications,
    std::unordered_map<core::TermId, core::TermId>& proxy_by_real_term,
    std::vector<core::TermId>& lra_definition_atoms, std::size_t& reused_real_terms) {
  core::TermManager& terms = frontend.terms();
  const core::Term& term = terms.get(term_id);
  if (term.sort != terms.symbols().real_sort()) {
    return term_id;
  }

  const core::TermId arithmetic_term =
      purify_arithmetic_term(frontend, term_id, proxy_by_app, app_definitions, reused_applications,
                             proxy_by_real_term, lra_definition_atoms, reused_real_terms);
  const core::Term& rewritten = terms.get(arithmetic_term);
  if (rewritten.kind == core::TermKind::Constant) {
    return arithmetic_term;
  }

  const auto found = proxy_by_real_term.find(arithmetic_term);
  if (found != proxy_by_real_term.end()) {
    ++reused_real_terms;
    return found->second;
  }
  const core::TermId proxy = frontend.fresh_internal_constant(rewritten.sort, "no_real_arg");
  proxy_by_real_term.emplace(arithmetic_term, proxy);
  lra_definition_atoms.push_back(terms.mk_le(proxy, arithmetic_term));
  lra_definition_atoms.push_back(terms.mk_le(arithmetic_term, proxy));
  return proxy;
}

core::TermId purify_euf_term(Frontend& frontend, core::TermId term_id,
                             std::unordered_map<core::TermId, core::TermId>& proxy_by_app,
                             std::vector<core::TermId>& app_definitions,
                             std::size_t& reused_applications,
                             std::unordered_map<core::TermId, core::TermId>& proxy_by_real_term,
                             std::vector<core::TermId>& lra_definition_atoms,
                             std::size_t& reused_real_terms) {
  core::TermManager& terms = frontend.terms();
  const core::Term& term = terms.get(term_id);
  const core::TermKind kind = term.kind;
  const core::SortId sort = term.sort;
  const core::SymbolId symbol = term.symbol;
  const std::vector<core::TermId> term_children = term.children;
  if (sort == terms.symbols().real_sort()) {
    return purify_euf_real_argument(frontend, term_id, proxy_by_app, app_definitions,
                                    reused_applications, proxy_by_real_term, lra_definition_atoms,
                                    reused_real_terms);
  }

  if (kind != core::TermKind::Application) {
    return term_id;
  }

  std::vector<core::TermId> children;
  children.reserve(term_children.size());
  bool changed = false;
  for (const core::TermId child : term_children) {
    const core::TermId rewritten =
        purify_euf_term(frontend, child, proxy_by_app, app_definitions, reused_applications,
                        proxy_by_real_term, lra_definition_atoms, reused_real_terms);
    changed = changed || rewritten != child;
    children.push_back(rewritten);
  }
  return changed ? terms.mk_app(symbol, std::move(children)) : term_id;
}

core::TermId purify_mixed_formula(
    Frontend& frontend, core::TermId term_id,
    std::unordered_map<core::TermId, core::TermId>& proxy_by_app,
    std::vector<core::TermId>& app_definitions, std::size_t& reused_applications,
    std::unordered_map<core::TermId, core::TermId>& proxy_by_real_term,
    std::vector<core::TermId>& lra_definition_atoms, std::size_t& reused_real_terms) {
  core::TermManager& terms = frontend.terms();
  const core::Term& term = terms.get(term_id);
  const core::TermKind kind = term.kind;
  const core::SortId sort = term.sort;
  const std::vector<core::TermId> term_children = term.children;
  switch (kind) {
    case core::TermKind::Le: {
      const core::TermId lhs = purify_arithmetic_term(
          frontend, term_children.at(0), proxy_by_app, app_definitions, reused_applications,
          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      const core::TermId rhs = purify_arithmetic_term(
          frontend, term_children.at(1), proxy_by_app, app_definitions, reused_applications,
          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      return lhs == term_children.at(0) && rhs == term_children.at(1) ? term_id
                                                                      : terms.mk_le(lhs, rhs);
    }
    case core::TermKind::Lt: {
      const core::TermId lhs = purify_arithmetic_term(
          frontend, term_children.at(0), proxy_by_app, app_definitions, reused_applications,
          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      const core::TermId rhs = purify_arithmetic_term(
          frontend, term_children.at(1), proxy_by_app, app_definitions, reused_applications,
          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      return lhs == term_children.at(0) && rhs == term_children.at(1) ? term_id
                                                                      : terms.mk_lt(lhs, rhs);
    }
    case core::TermKind::Equality:
      if (terms.get(term_children.at(0)).sort == terms.symbols().real_sort()) {
        const core::TermId lhs = purify_arithmetic_term(
            frontend, term_children.at(0), proxy_by_app, app_definitions, reused_applications,
            proxy_by_real_term, lra_definition_atoms, reused_real_terms);
        const core::TermId rhs = purify_arithmetic_term(
            frontend, term_children.at(1), proxy_by_app, app_definitions, reused_applications,
            proxy_by_real_term, lra_definition_atoms, reused_real_terms);
        return lhs == term_children.at(0) && rhs == term_children.at(1) ? term_id
                                                                        : terms.mk_eq(lhs, rhs);
      }
      {
        const core::TermId lhs = purify_euf_term(
            frontend, term_children.at(0), proxy_by_app, app_definitions, reused_applications,
            proxy_by_real_term, lra_definition_atoms, reused_real_terms);
        const core::TermId rhs = purify_euf_term(
            frontend, term_children.at(1), proxy_by_app, app_definitions, reused_applications,
            proxy_by_real_term, lra_definition_atoms, reused_real_terms);
        return lhs == term_children.at(0) && rhs == term_children.at(1) ? term_id
                                                                        : terms.mk_eq(lhs, rhs);
      }
    case core::TermKind::Not: {
      const core::TermId child = purify_mixed_formula(
          frontend, term_children.at(0), proxy_by_app, app_definitions, reused_applications,
          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      return child == term_children.at(0) ? term_id : terms.mk_not(child);
    }
    case core::TermKind::And:
    case core::TermKind::Or: {
      std::vector<core::TermId> children;
      children.reserve(term_children.size());
      bool changed = false;
      for (const core::TermId child : term_children) {
        const core::TermId rewritten = purify_mixed_formula(
            frontend, child, proxy_by_app, app_definitions, reused_applications, proxy_by_real_term,
            lra_definition_atoms, reused_real_terms);
        changed = changed || rewritten != child;
        children.push_back(rewritten);
      }
      if (!changed) {
        return term_id;
      }
      return kind == core::TermKind::And ? terms.mk_and(std::move(children))
                                         : terms.mk_or(std::move(children));
    }
    case core::TermKind::Implies: {
      const core::TermId lhs = purify_mixed_formula(
          frontend, term_children.at(0), proxy_by_app, app_definitions, reused_applications,
          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      const core::TermId rhs = purify_mixed_formula(
          frontend, term_children.at(1), proxy_by_app, app_definitions, reused_applications,
          proxy_by_real_term, lra_definition_atoms, reused_real_terms);
      return lhs == term_children.at(0) && rhs == term_children.at(1) ? term_id
                                                                      : terms.mk_implies(lhs, rhs);
    }
    case core::TermKind::Application:
      if (sort == terms.symbols().bool_sort()) {
        return purify_euf_term(frontend, term_id, proxy_by_app, app_definitions,
                               reused_applications, proxy_by_real_term, lra_definition_atoms,
                               reused_real_terms);
      }
      return term_id;
    default:
      return term_id;
  }
}

MixedTheoryPurificationResult purify_mixed_arithmetic_applications(
    Frontend& frontend, const std::vector<core::TermId>& assertions) {
  std::unordered_map<core::TermId, core::TermId> proxy_by_application;
  std::unordered_map<core::TermId, core::TermId> proxy_by_real_term;
  std::vector<core::TermId> rewritten_assertions;
  std::vector<core::TermId> app_definitions;
  std::vector<core::TermId> lra_definition_atoms;
  std::size_t reused_applications = 0;
  std::size_t reused_real_terms = 0;
  rewritten_assertions.reserve(assertions.size());
  app_definitions.reserve(assertions.size());
  lra_definition_atoms.reserve(assertions.size() * 2);
  for (const core::TermId assertion : assertions) {
    rewritten_assertions.push_back(purify_mixed_formula(
        frontend, assertion, proxy_by_application, app_definitions, reused_applications,
        proxy_by_real_term, lra_definition_atoms, reused_real_terms));
  }
  rewritten_assertions.insert(rewritten_assertions.end(), app_definitions.begin(),
                              app_definitions.end());
  rewritten_assertions.insert(rewritten_assertions.end(), lra_definition_atoms.begin(),
                              lra_definition_atoms.end());
  return MixedTheoryPurificationResult{std::move(rewritten_assertions), app_definitions.size(),
                                       proxy_by_real_term.size(), reused_applications,
                                       reused_real_terms};
}

// Incremental QF_LRA theory check (Dutertre & de Moura Section 4): copies
// the precomputed base tableau (cheap relative to re-normalizing every
// atom from its term representation, which the copy skips entirely) and
// asserts bounds for the round's assigned atoms into the copy.
//
// Only compiled when CaDiCaL's external-propagator API isn't available --
// when it is, OnlineLraPropagator (below) is strictly better (real
// incremental reuse via LraTableau::push/pop) and is always preferred, so
// this would otherwise be dead code.
std::optional<sat::Clause> find_incremental_lra_theory_conflict(
    const PreparedLraTheory& prepared, const std::vector<AssignedTheoryAtom>& atoms) {
  if (atoms.size() != prepared.atoms.size()) {
    throw UnsupportedInput("internal theory atom mapping mismatch");
  }

  theory::arithmetic::LraTableau tableau = prepared.base_tableau;
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    const AssignedTheoryAtom& atom = atoms[i];
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared.atoms[i];
    if (!prepared_atom.is_arithmetic || atom.filtered) {
      continue;
    }
    const auto conflict = assert_lra_atom_bound(
        tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom.value,
        prepared_atom.asserts_lower_when_true, blocking_lit(atom));
    if (conflict.has_value()) {
      return conflict.value();
    }
  }

  return tableau.check();
}

#if PRIMO_HAVE_IPASIR
// Row-based/interval bound propagation (Dutertre & de Moura Section 4,
// generalized beyond scan_for_propagations' same-slack case): the implied
// upper (want_upper=true) or lower (want_upper=false) bound on `basic_var`
// from its row's terms' CURRENT bounds -- basic_var = sum(coeff_j * var_j),
// so basic_var's maximum is the sum of each term's own maximum contribution
// (var_j's upper bound if coeff_j is positive, since a bigger var_j means a
// bigger term then; var_j's lower bound if coeff_j is negative), and
// symmetrically for the minimum/lower case. Since item H1 asserts a
// single-variable atom's bound directly on the variable (rather than wrapping
// it in a slack), each term's bound is simply tableau.upper_bound/
// lower_bound(var) -- no indirection, and strictly more reaching than the
// previous single_variable_slack lookup, which could only see a term whose
// formula happened to contain a bare coefficient-1 atom over it.
// Returns nullopt if the row exceeds the size cap or a term lacks the
// needed-direction bound (an unbounded term makes the whole sum unbounded in
// that direction, not just weaker). The returned reasons are one asserting
// literal per row term actually used -- a genuine multi-literal antecedent,
// unlike same-variable propagation's single-literal one.
std::optional<std::pair<theory::arithmetic::DeltaRational, std::vector<sat::Lit>>>
compute_implied_bound(const theory::arithmetic::LraTableau& tableau,
                      theory::arithmetic::LraVarId basic_var, bool want_upper) {
  const auto& row = tableau.row(basic_var);
  if (row.size() > kRowBasedPropagationMaxRowSize) {
    return std::nullopt;
  }
  theory::arithmetic::DeltaRational total =
      theory::arithmetic::DeltaRational::from_rational(util::Rational(0));
  std::vector<sat::Lit> reasons;
  reasons.reserve(row.size());
  for (const auto& [var, coeff] : row) {
    const bool need_upper_of_var = want_upper == (coeff.sign() > 0);
    const auto bound = need_upper_of_var ? tableau.upper_bound(var) : tableau.lower_bound(var);
    if (!bound.has_value()) {
      return std::nullopt;
    }
    total = total + bound->value.scaled(coeff);
    reasons.push_back(bound->reason);
  }
  return std::make_pair(total, std::move(reasons));
}

// Competitiveness-roadmap item 7's reverse direction: derive an implied
// bound on one *other* term of basic_var's row (target_var, with
// coefficient target_coeff in that row) from basic_var's own current
// bound plus the row's remaining terms' bounds -- the rearrangement
// `basic_var = target_coeff*target_var + Σ_other coeff_j*var_j`.
//
// Sign rule (verified algebraically, not guessed): treating basic_var as
// a term of coefficient +1 and each *other* row term as a term of
// coefficient -coeff_j (moving them to the other side) maps this onto
// compute_implied_bound's own existing sign convention:
//   want_upper_of_basic = want_upper_of_target == (target_coeff.sign() > 0)
//   need_upper_of_var_j = want_upper_of_basic != (coeff_j.sign() > 0)
// Missing any needed bound (basic_var's own or any other term's) makes this
// nullopt for this round, same all-or-nothing behavior as
// compute_implied_bound.
std::optional<std::pair<theory::arithmetic::DeltaRational, std::vector<sat::Lit>>>
compute_implied_child_bound(const theory::arithmetic::LraTableau& tableau,
                            theory::arithmetic::LraVarId basic_var,
                            theory::arithmetic::LraVarId target_var,
                            const util::Rational& target_coeff, bool want_upper_of_target) {
  const auto& row = tableau.row(basic_var);
  if (row.size() > kBidirectionalRowPropagationMaxRowSize) {
    return std::nullopt;
  }
  const bool want_upper_of_basic = want_upper_of_target == (target_coeff.sign() > 0);
  const auto basic_bound =
      want_upper_of_basic ? tableau.upper_bound(basic_var) : tableau.lower_bound(basic_var);
  if (!basic_bound.has_value()) {
    return std::nullopt;
  }
  theory::arithmetic::DeltaRational total = basic_bound->value;
  std::vector<sat::Lit> reasons;
  reasons.reserve(row.size());
  reasons.push_back(basic_bound->reason);
  for (const auto& [var, coeff] : row) {
    if (var == target_var) {
      continue;
    }
    const bool need_upper_of_var = want_upper_of_basic != (coeff.sign() > 0);
    const auto bound = need_upper_of_var ? tableau.upper_bound(var) : tableau.lower_bound(var);
    if (!bound.has_value()) {
      return std::nullopt;
    }
    total = total - bound->value.scaled(coeff);
    reasons.push_back(bound->reason);
  }
  const util::Rational inv = util::Rational(1) / target_coeff;
  return std::make_pair(total.scaled(inv), std::move(reasons));
}

// Publishes the prepared tableau's structural shape: how many variables it
// has, how many of them define a row, and how many nonzero coefficients those
// rows hold in total. Structural, not search state -- rows are all added
// before the first push() (see LraTableau's class comment), so this is a
// property of the formula and of how the frontend chose to encode it.
//
// These exist because the tableau's shape was previously invisible: item H1's
// finding that 36-95% of rows on the lagging families were trivial `s = c*x`
// slack rows had to be produced with throwaway instrumentation, and the
// fill-in growth it also measured went unnoticed through eight roadmap items
// for want of exactly this. See docs/qf-lra-heuristics.md.
void publish_lra_tableau_shape_stats(const theory::arithmetic::LraTableau& tableau,
                                     util::Statistics& statistics) {
  std::size_t rows = 0;
  std::size_t nonzeros = 0;
  for (theory::arithmetic::LraVarId var = 0; var < tableau.variable_count(); ++var) {
    if (!tableau.is_basic(var)) {
      continue;
    }
    ++rows;
    nonzeros += tableau.row(var).size();
  }
  statistics.set(util::StatKey::LraTableauVars, tableau.variable_count());
  statistics.set(util::StatKey::LraRows, rows);
  statistics.set(util::StatKey::LraRowNonzeros, nonzeros);
}

// Trail-synced CaDiCaL external propagator for QF_LRA (Milestone 3a of the
// QF_LRA plan): mirrors OnlineEufPropagator structurally. cb_propagate()
// delivers same-slack propagations (Milestone 3b, scan_for_propagations)
// and, opt-in, row-based/interval propagations (competitiveness-roadmap
// item 2, scan_row_based_propagations) -- both gated behind
// propagation_enabled_/row_propagation_enabled_ respectively, off entirely
// falls back to conflict-detection-only, mirroring EUF's own
// EufPropagationMode::Conflict line.
//
// Owns its own LraTableau (copied once from PreparedLraTheory's base, not
// per SAT round), kept in sync with the CaDiCaL trail via
// notify_new_decision_level/notify_backtrack calling LraTableau::push()/
// pop() -- this is what actually exercises push/pop for cross-round reuse,
// unlike solve_qf_lra's other path (find_incremental_lra_theory_conflict),
// which copies the base tableau fresh every round.
class OnlineLraPropagator final : public CaDiCaL::ExternalPropagator {
 public:
  OnlineLraPropagator(const PreparedLraTheory& prepared,
                      const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms,
                      bool propagation_enabled, bool row_propagation_enabled,
                      bool bidirectional_row_propagation_enabled, bool theory_decisions_enabled,
                      util::Statistics& statistics)
      : theory_atoms_(theory_atoms),
        prepared_(prepared),
        tableau_(prepared.base_tableau),
        propagation_enabled_(propagation_enabled),
        row_propagation_enabled_(row_propagation_enabled),
        bidirectional_row_propagation_enabled_(bidirectional_row_propagation_enabled),
        theory_decisions_enabled_(theory_decisions_enabled),
        statistics_(statistics) {
    int max_theory_var = 0;
    for (const BooleanClausifier::TheoryAtom& atom : theory_atoms_) {
      max_theory_var =
          std::max(max_theory_var, atom.lit.value > 0 ? atom.lit.value : -atom.lit.value);
    }
    observed_vars_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    assigned_.assign(static_cast<std::size_t>(max_theory_var) + 1, 0);
    atom_index_by_var_.assign(static_cast<std::size_t>(max_theory_var) + 1, theory_atoms_.size());
    for (std::size_t i = 0; i < theory_atoms_.size(); ++i) {
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[i];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      observed_vars_[static_cast<std::size_t>(var)] = 1;
      atom_index_by_var_[static_cast<std::size_t>(var)] = i;
    }
  }

#if PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR
  void notify_assignment(const std::vector<int>& lits) override {
    for (const int lit : lits) {
      notify_assignment_lit(lit);
    }
  }
#else
  void notify_assignment(int lit, bool /*is_fixed*/) override { notify_assignment_lit(lit); }
#endif

  void notify_new_decision_level() override {
    checkpoints_.push_back(tableau_.push());
    level_starts_.push_back(var_trail_.size());
  }

  void notify_backtrack(size_t new_level) override {
    if (new_level < checkpoints_.size()) {
      tableau_.pop(checkpoints_[new_level]);
      checkpoints_.resize(new_level);
    }
    if (new_level < level_starts_.size()) {
      const std::size_t keep = level_starts_[new_level];
      while (var_trail_.size() > keep) {
        const int var = var_trail_.back();
        var_trail_.pop_back();
        assigned_[static_cast<std::size_t>(var)] = 0;
        // Only erase reasons_ entries for literals actually unassigned by
        // this backtrack -- CaDiCaL can still legitimately ask (e.g. during
        // conflict analysis of a *later* conflict) for the reason of an
        // earlier propagation that remains on the trail after backtracking
        // to new_level, and clearing it unconditionally here would answer
        // that with an empty (invalid) reason clause.
        reasons_.erase(var);
        reasons_.erase(-var);
      }
      level_starts_.resize(new_level);
    }
    conflict_.reset();
    // Sound to drop unconditionally per notify_assignment_lit's own
    // verified invariant: a new decision level can never open while a
    // deferred check is still outstanding (cb_has_external_clause -- and
    // thus run_deferred_check_if_needed() -- is always consulted first),
    // so needs_check_ can never be true across a backtrack in a way that
    // would lose an actually-outstanding repair.
    needs_check_ = false;
    external_clause_.clear();
    external_clause_index_ = 0;
    // pending_ entries are just candidates, not delivered propagations --
    // safe to drop unconditionally (unlike reasons_ above): at worst a
    // still-valid candidate gets rediscovered next time its slack changes.
    pending_.clear();
    reason_stream_active_lit_ = 0;
    reason_stream_index_ = 0;
  }

  bool cb_check_found_model(const std::vector<int>& /*model*/) override { return true; }

  // Competitiveness-roadmap item 8: called by CaDiCaL right before it would
  // otherwise make a generic VSIDS decision (Internal::ask_decision(),
  // contrib/src/cadical/src/external_propagate.cpp) -- supplies the next
  // not-yet-decided arithmetic atom's TRUE literal, advancing a persistent
  // rotating cursor across calls. A fixed, deterministic decision ORDER
  // over arithmetic atoms, not a per-candidate cost analysis.
  //
  // An earlier version of this (see git history around commit 17f8faf)
  // ranked candidates by a cheap per-atom infeasibility-cost estimate
  // instead of just taking the next one -- re-measured against this much
  // simpler alternative on the same 9 real-benchmark files (docs/
  // arithmetic.md's Milestone roadmap has the full trail), the cost-ranked
  // version was *worse* than plain VSIDS on 3 of them (solved regressed to
  // timed-out at the same budget) and worse than this round-robin version
  // on 8/9 -- sometimes dramatically (a 96% wall-clock regression on one
  // file). The ranking wasn't just expensive overhead; it was actively
  // steering search into worse territory. This version -- "the theory
  // decides, in a stable order, no per-candidate arithmetic at all" -- is
  // strictly better on this evidence, and the only one of the three that
  // solves the campaign's hardest file (sc-27) at all within a shared
  // budget. Returning 0 defers to VSIDS -- only happens once every
  // arithmetic atom is already decided.
  int cb_decide() override {
    if (!theory_decisions_enabled_ || theory_atoms_.empty()) {
      return 0;
    }
    const std::size_t atom_count = theory_atoms_.size();
    for (std::size_t scanned = 0; scanned < atom_count; ++scanned) {
      const std::size_t atom_index = decide_cursor_;
      decide_cursor_ = (decide_cursor_ + 1) % atom_count;
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
      if (!prepared_atom.is_arithmetic) {
        continue;
      }
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (is_assigned_var(var)) {
        continue;
      }
      statistics_.increment(kStatLraTheoryDecisions);
      return atom.lit.value;
    }
    return 0;
  }

  // Same-slack "deriving satisfied atoms" propagation (Dutertre & de
  // Moura, Section 4.2): pops literals queued by notify_assignment_lit's
  // propagation scan. Never propagates once a conflict is pending this
  // round (mirrors notify_assignment_lit's own early-return), and lazily
  // skips any queued entry whose var got assigned some other way in the
  // meantime (a decision, a different propagation) rather than proactively
  // pruning pending_ on every assignment.
  int cb_propagate() override {
    if (conflict_.has_value()) {
      return 0;
    }
    while (!pending_.empty()) {
      PendingPropagation candidate = std::move(pending_.front());
      pending_.pop_front();
      const int var = candidate.lit > 0 ? candidate.lit : -candidate.lit;
      if (is_assigned_var(var)) {
        continue;
      }
      const int lit = candidate.lit;
      const bool row_based = candidate.row_based;
      const bool bidirectional = candidate.bidirectional;
      reasons_[lit] = std::move(candidate.reasons);
      statistics_.increment(kStatLraTheoryPropagations);
      if (row_based) {
        statistics_.increment(kStatLraRowPropagations);
      }
      if (bidirectional) {
        statistics_.increment(kStatLraBidirectionalRowPropagations);
      }
      return lit;
    }
    return 0;
  }

  int cb_add_reason_clause_lit(int propagated_lit) override {
    const auto found = reasons_.find(propagated_lit);
    if (found == reasons_.end()) {
      return 0;
    }
    if (reason_stream_active_lit_ != propagated_lit) {
      reason_stream_active_lit_ = propagated_lit;
      reason_stream_index_ = 0;
    }
    if (reason_stream_index_ == 0) {
      ++reason_stream_index_;
      return propagated_lit;
    }
    const std::size_t antecedent_index = static_cast<std::size_t>(reason_stream_index_ - 1);
    if (antecedent_index < found->second.size()) {
      ++reason_stream_index_;
      return found->second[antecedent_index].value;
    }
    reason_stream_active_lit_ = 0;
    reason_stream_index_ = 0;
    return 0;
  }

  // Authoritative once backend.solve() returns SAT, for the same reason
  // OnlineEufPropagator::incremental_conflict() is: cb_has_external_clause()
  // (which runs the deferred Check/Pivot repair -- see
  // run_deferred_check_if_needed()) is always asked, and any resulting
  // conflict resolved, before CaDiCaL's own main loop can reach a
  // decision or declare a model satisfied (see notify_assignment_lit's
  // comment for the verified invariant), so there is nothing left to
  // recompute once solve() actually returns SAT.
  std::optional<sat::Clause> conflict() const { return conflict_; }
  const theory::arithmetic::LraTableau& tableau() const { return tableau_; }

#if PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR
  bool cb_has_external_clause(bool& is_forgettable) override {
    run_deferred_check_if_needed();
    if (!conflict_.has_value()) {
      return false;
    }
    external_clause_ = conflict_.value();
    external_clause_index_ = 0;
    is_forgettable = false;
    return true;
  }
#else
  bool cb_has_external_clause() override {
    run_deferred_check_if_needed();
    if (!conflict_.has_value()) {
      return false;
    }
    external_clause_ = conflict_.value();
    external_clause_index_ = 0;
    return true;
  }
#endif

  int cb_add_external_clause_lit() override {
    if (external_clause_index_ >= external_clause_.size()) {
      external_clause_.clear();
      external_clause_index_ = 0;
      return 0;
    }
    return external_clause_[external_clause_index_++].value;
  }

 private:
  bool is_observed_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < observed_vars_.size() &&
           observed_vars_[static_cast<std::size_t>(var)] != 0;
  }

  bool is_assigned_var(int var) const {
    return var >= 0 && static_cast<std::size_t>(var) < assigned_.size() &&
           assigned_[static_cast<std::size_t>(var)] != 0;
  }

  void publish_elimination_stats() {
    statistics_.set(util::StatKey::LraFixedVariables, tableau_.fixed_variable_count());
    statistics_.set(util::StatKey::LraEliminationPivots, tableau_.elimination_pivot_count());
  }

  void notify_assignment_lit(int lit) {
    if (conflict_.has_value()) {
      // Already found a conflict this round; CaDiCaL hasn't pulled it via
      // cb_has_external_clause yet, so further asserts would just be
      // wasted (and potentially confusing, since assert_lra_atom_bound
      // isn't meant to be called against an already-inconsistent tableau).
      return;
    }
    const int var = lit > 0 ? lit : -lit;
    if (!is_observed_var(var)) {
      return;
    }
    assigned_[static_cast<std::size_t>(var)] = 1;
    var_trail_.push_back(var);
    const std::size_t atom_index = atom_index_by_var_[static_cast<std::size_t>(var)];
    const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
    if (!prepared_atom.is_arithmetic) {
      return;
    }
    const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
    const bool atom_value = atom.lit.value > 0 ? lit > 0 : lit < 0;
    const AssignedTheoryAtom assigned =
        assigned_theory_atom(PureTheoryFilterMode::Off, atom, atom_value);
    const auto direct_conflict = assert_lra_atom_bound(
        tableau_, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs, atom_value,
        prepared_atom.asserts_lower_when_true, blocking_lit(assigned));
    // Item H7's counters move inside assert_upper/assert_lower, not inside
    // check(), so they are published here rather than alongside the pivot
    // counters in run_deferred_check_if_needed(). They are cumulative
    // totals, so set() (not increment()) is what stays correct across the
    // repeated calls -- and this is the only place they can change.
    publish_elimination_stats();
    if (direct_conflict.has_value()) {
      conflict_ = direct_conflict;
      // needs_check_ may already be true from an earlier, non-conflicting
      // literal in this same batch -- clear it, or run_deferred_check_if_
      // needed() would later overwrite this genuine direct conflict with
      // a stale tableau_.check() result. (The offending bound itself was
      // never actually stored by assert_upper/assert_lower's own direct-
      // conflict branch, so check() alone wouldn't even rediscover it.)
      needs_check_ = false;
      return;
    }
    if (propagation_enabled_) {
      scan_for_propagations(atom_index, prepared_atom.bound_var);
      if (row_propagation_enabled_) {
        scan_row_based_propagations(prepared_atom.bound_var);
      }
    }
    // Defer the expensive Check/Pivot repair rather than running it
    // eagerly after every single assignment (decisions *and* propagated
    // literals both land here): notify_assignment_lit can be called
    // tens of thousands of times more often than CaDiCaL actually needs
    // a verdict from us. Verified against CaDiCaL's own source
    // (contrib/src/cadical/src/{internal,external_propagate}.cpp): with
    // this propagator in eager mode (is_lazy left at its default
    // `false`), cb_has_external_clause() is asked exactly once per
    // quiescence point (once per decision plus once per conflict/
    // backjump, not once per literal), and cb_propagate() is always
    // fully drained *before* it's asked -- so same-slack/row-based
    // propagation above (which reads only bound state, never the
    // current assignment) is completely unaffected by deferring this.
    // See run_deferred_check_if_needed(), called from both
    // cb_has_external_clause() overloads.
    needs_check_ = true;
  }

  // The actual Check/Pivot repair, run lazily right before CaDiCaL is
  // told whether there's a conflict clause to add -- see
  // notify_assignment_lit's own comment for why this specific hook
  // point is both correct (propagation doesn't depend on it) and the
  // right frequency (once per quiescence point, not once per literal).
  void run_deferred_check_if_needed() {
    if (!needs_check_) {
      return;
    }
    needs_check_ = false;
    // Belt-and-suspenders: notify_assignment_lit's direct-conflict branch
    // already clears needs_check_ itself (see its own comment), but never
    // overwrite an already-discovered conflict with a fresh
    // tableau_.check() result regardless -- tableau_.check() only sees
    // whatever bounds actually made it into the tableau, and a rejected
    // direct-conflict bound never does.
    if (conflict_.has_value()) {
      return;
    }
    const std::size_t fallbacks_before = tableau_.bland_fallback_count();
    const std::size_t multi_row_conflicts_before = tableau_.soi_multi_row_conflict_count();
    const std::size_t quickxplain_calls_before = tableau_.soi_quickxplain_call_count();
    const std::size_t pivots_before = tableau_.pivot_count();
    const std::size_t row_updates_before = tableau_.row_update_count();
    const std::size_t term_ops_before = tableau_.row_term_op_count();
    statistics_.increment(kStatLraOnlineCheckCalls);
    conflict_ = tableau_.check();
    statistics_.increment(kStatLraBlandFallbacks,
                          tableau_.bland_fallback_count() - fallbacks_before);
    statistics_.increment(kStatLraSoiMultiRowConflicts,
                          tableau_.soi_multi_row_conflict_count() - multi_row_conflicts_before);
    statistics_.increment(kStatLraSoiQuickxplainCalls,
                          tableau_.soi_quickxplain_call_count() - quickxplain_calls_before);
    statistics_.increment(kStatLraPivots, tableau_.pivot_count() - pivots_before);
    statistics_.increment(kStatLraPivotRowUpdates,
                          tableau_.row_update_count() - row_updates_before);
    statistics_.increment(kStatLraPivotTermOps, tableau_.row_term_op_count() - term_ops_before);
  }

  // Scans the other atoms sharing `slack` with the atom that was just
  // asserted (index `triggering_atom_index`, already applied to `tableau_`
  // with no direct conflict) for ones now entailed true/false by the
  // slack's current upper/lower bound (Dutertre & de Moura Section 4.2).
  // Only needs to look at this one slack: bounds only ever change via
  // explicit assert_upper/assert_lower (never via check()'s pivots, which
  // only touch assignments/row structure), so no other slack's bounds
  // could have changed as a side effect of this assertion.
  void scan_for_propagations(std::size_t triggering_atom_index,
                             theory::arithmetic::LraVarId bound_var) {
    const auto found = prepared_.atoms_by_bound_var.find(bound_var);
    if (found == prepared_.atoms_by_bound_var.end()) {
      return;
    }
    for (const std::size_t atom_index : found->second) {
      if (atom_index == triggering_atom_index) {
        continue;
      }
      const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      if (is_assigned_var(var)) {
        continue;
      }
      const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
      const auto entails_true = bound_entails(
          tableau_, prepared_atom.bound_var,
          bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                          prepared_atom.asserts_lower_when_true, /*atom_value=*/true));
      if (entails_true.has_value()) {
        pending_.push_back(PendingPropagation{atom.lit.value, {*entails_true}, false});
        continue;
      }
      const auto entails_false = bound_entails(
          tableau_, prepared_atom.bound_var,
          bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                          prepared_atom.asserts_lower_when_true, /*atom_value=*/false));
      if (entails_false.has_value()) {
        pending_.push_back(PendingPropagation{-atom.lit.value, {*entails_false}, false});
      }
    }
  }

  // Whether the implied bound `implied` on `basic_var` is tight enough to
  // entail the atom whose truth assignment would impose `assertion`. Shares
  // bound_assertion()'s direction convention, so a flipped atom's TRUE case
  // is checked against an implied *lower* bound and vice versa.
  std::optional<std::vector<sat::Lit>> implied_bound_entails(theory::arithmetic::LraVarId basic_var,
                                                             const BoundAssertion& assertion) {
    auto implied = compute_implied_bound(tableau_, basic_var, assertion.is_upper);
    if (!implied.has_value()) {
      return std::nullopt;
    }
    const bool tight =
        assertion.is_upper ? implied->first <= assertion.value : implied->first >= assertion.value;
    if (!tight) {
      return std::nullopt;
    }
    return std::move(implied->second);
  }

  // Row-based/interval bound propagation (Dutertre & de Moura Section 4,
  // generalizing scan_for_propagations to a *derived* rather than
  // directly-asserted bound): `changed_var`'s bound was just tightened, so
  // any row currently mentioning it (tableau_.occurs_in) could now have a
  // tighter implied bound. For each such row, compute the implied
  // upper/lower via compute_implied_bound and check entailment for the atoms
  // bounding that row's own basic variable.
  //
  // Before item H1 this could only trigger when `changed_var` happened to be
  // a slack wrapping exactly one coefficient-1 variable, since bounds only
  // ever lived on slacks; now that single-variable atoms bound their variable
  // directly, any bound change is a candidate and the fanout cap below is
  // what keeps the cost bounded.
  void scan_row_based_propagations(theory::arithmetic::LraVarId changed_var) {
    const std::vector<theory::arithmetic::LraVarId>& affected_rows =
        tableau_.occurs_in(changed_var);
    if (affected_rows.empty() || affected_rows.size() > kRowBasedPropagationMaxFanout) {
      return;
    }
    for (const theory::arithmetic::LraVarId basic_var : affected_rows) {
      const auto found = prepared_.atoms_by_bound_var.find(basic_var);
      if (found == prepared_.atoms_by_bound_var.end()) {
        continue;
      }
      for (const std::size_t atom_index : found->second) {
        const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
        const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
        if (is_assigned_var(var)) {
          continue;
        }
        const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
        auto reasons = implied_bound_entails(
            basic_var, bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                                       prepared_atom.asserts_lower_when_true, /*atom_value=*/true));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{atom.lit.value, std::move(*reasons), true});
          continue;
        }
        reasons = implied_bound_entails(
            basic_var,
            bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                            prepared_atom.asserts_lower_when_true, /*atom_value=*/false));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{-atom.lit.value, std::move(*reasons), true});
        }
      }
      if (bidirectional_row_propagation_enabled_ &&
          affected_rows.size() <= kBidirectionalRowPropagationMaxFanout) {
        scan_row_child_propagations(basic_var, changed_var);
      }
    }
  }

  // Competitiveness-roadmap item 7 (the reverse direction of
  // scan_row_based_propagations): for each *other* term of basic_var's
  // row (skipping exclude_var, whose own bound is what just triggered
  // this scan -- nothing new to derive about it from itself), check
  // whether compute_implied_child_bound now entails one of that term's
  // own atoms. Gated by its own (smaller) size caps -- see their comment.
  void scan_row_child_propagations(theory::arithmetic::LraVarId basic_var,
                                   theory::arithmetic::LraVarId exclude_var) {
    const auto& row = tableau_.row(basic_var);
    if (row.size() > kBidirectionalRowPropagationMaxRowSize) {
      return;
    }
    for (const auto& [target_var, target_coeff] : row) {
      if (target_var == exclude_var) {
        continue;
      }
      const auto found = prepared_.atoms_by_bound_var.find(target_var);
      if (found == prepared_.atoms_by_bound_var.end()) {
        continue;
      }
      for (const std::size_t atom_index : found->second) {
        const BooleanClausifier::TheoryAtom& atom = theory_atoms_[atom_index];
        const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
        if (is_assigned_var(var)) {
          continue;
        }
        const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_.atoms[atom_index];
        auto reasons = implied_child_bound_entails(
            basic_var, target_var, target_coeff,
            bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                            prepared_atom.asserts_lower_when_true, /*atom_value=*/true));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{atom.lit.value, std::move(*reasons), true, true});
          continue;
        }
        reasons = implied_child_bound_entails(
            basic_var, target_var, target_coeff,
            bound_assertion(prepared_atom.relation, prepared_atom.rhs,
                            prepared_atom.asserts_lower_when_true, /*atom_value=*/false));
        if (reasons.has_value()) {
          pending_.push_back(PendingPropagation{-atom.lit.value, std::move(*reasons), true, true});
        }
      }
    }
  }

  // implied_bound_entails' counterpart for the reverse (child-tightening)
  // direction -- same convention, same all-or-nothing behaviour.
  std::optional<std::vector<sat::Lit>> implied_child_bound_entails(
      theory::arithmetic::LraVarId basic_var, theory::arithmetic::LraVarId target_var,
      const util::Rational& target_coeff, const BoundAssertion& assertion) {
    auto implied = compute_implied_child_bound(tableau_, basic_var, target_var, target_coeff,
                                               assertion.is_upper);
    if (!implied.has_value()) {
      return std::nullopt;
    }
    const bool tight =
        assertion.is_upper ? implied->first <= assertion.value : implied->first >= assertion.value;
    if (!tight) {
      return std::nullopt;
    }
    return std::move(implied->second);
  }

  // A queued propagation candidate: the CaDiCaL literal to propagate, its
  // antecedent reason literals (one for same-slack propagation; up to a
  // row's term count for row-based propagation), and whether it came from
  // the row-based scan (for the LraRowPropagations breakdown stat).
  struct PendingPropagation {
    int lit;
    std::vector<sat::Lit> reasons;
    bool row_based;
    bool bidirectional = false;  // competitiveness-roadmap item 7
  };

  const std::vector<BooleanClausifier::TheoryAtom>& theory_atoms_;
  const PreparedLraTheory& prepared_;
  theory::arithmetic::LraTableau tableau_;
  bool propagation_enabled_;
  bool row_propagation_enabled_;
  bool bidirectional_row_propagation_enabled_;
  bool theory_decisions_enabled_;
  util::Statistics& statistics_;
  std::vector<char> observed_vars_;
  std::vector<char> assigned_;
  std::vector<int> var_trail_;
  std::vector<std::size_t> atom_index_by_var_;
  std::vector<std::size_t> checkpoints_;
  std::vector<std::size_t> level_starts_;
  std::deque<PendingPropagation> pending_;
  std::unordered_map<int, std::vector<sat::Lit>> reasons_;
  int reason_stream_active_lit_ = 0;
  int reason_stream_index_ = 0;
  std::optional<sat::Clause> conflict_;
  sat::Clause external_clause_;
  std::size_t external_clause_index_ = 0;
  // See notify_assignment_lit's comment: set instead of running
  // tableau_.check() eagerly per literal, resolved lazily by
  // run_deferred_check_if_needed() (called from cb_has_external_clause()).
  bool needs_check_ = false;
  // cb_decide()'s rotating position (competitiveness-roadmap item 8):
  // gives cb_decide() a stable, persistent round-robin order over
  // arithmetic atoms across calls, rather than always restarting from
  // index 0. Not reset on backtrack -- it's just a decision-order cursor,
  // not trail state; a stale position after backtracking is harmless
  // (worst case, this decision's order is less well-chosen than usual).
  std::size_t decide_cursor_ = 0;
};
#endif  // PRIMO_HAVE_IPASIR

CheckSatOutcome solve_qf_lra_impl(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const std::vector<core::TermId>& model_terms,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments,
    const CheckSatOptions& options, util::Statistics& statistics) {
  statistics.increment(kStatSolverQfLra);
  sat::CadicalBackend backend(options.cadical_options);
  const ActiveSolveStatsGuard active_solve_stats_guard(backend, statistics);
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
  publish_sat_stats(backend.statistics(), statistics);

  const PreparedLraTheory prepared_theory(
      terms, clausifier.theory_atoms(), lra_pivoting_rule_from_options(options.lra_pivoting_rule),
      options.lra_sparse_leaving, options.lra_fixed_elimination, options.produce_model);
  publish_lra_tableau_shape_stats(prepared_theory.base_tableau, statistics);

#if PRIMO_HAVE_IPASIR
  // The trail-synced online propagator (Milestone 3a) is always preferred
  // when CaDiCaL's external-propagator API is available: it's strictly
  // better than the copy-per-round fallback below (real incremental reuse
  // via LraTableau::push/pop instead of reasserting everything from
  // scratch every SAT round), and correctness has already been verified
  // against it via the fuzz gate (see docs/run-log.json).
  OnlineLraPropagator online_propagator(prepared_theory, clausifier.theory_atoms(),
                                        options.lra_propagation, options.lra_row_propagation,
                                        options.lra_bidirectional_row_propagation,
                                        options.lra_theory_decisions, statistics);
  backend.connect_external_propagator(&online_propagator);
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    backend.add_observed_var(var);
  }
#endif

  while (true) {
    statistics.increment(kStatSatSolveCalls);
    const sat::SatResult sat_result = backend.solve();
    publish_sat_stats(backend.statistics(), statistics);
    if (sat_result != sat::SatResult::Sat) {
      return CheckSatOutcome{map_sat_result(sat_result), "", statistics, ""};
    }
    statistics.increment(kStatLraTheoryChecks);

#if PRIMO_HAVE_IPASIR
    // Authoritative already -- see OnlineLraPropagator::conflict()'s
    // comment. Always nullopt in practice at this point (a real conflict
    // would have blocked CaDiCaL from ever returning SAT), but re-checking
    // costs nothing and mirrors solve_qf_uf's own belt-and-suspenders style.
    const std::optional<sat::Clause> conflict_clause = online_propagator.conflict();
#else
    const std::vector<AssignedTheoryAtom> assigned_atoms =
        assigned_atoms_from_backend(backend, clausifier.theory_atoms());

    const std::optional<sat::Clause> conflict_clause =
        find_incremental_lra_theory_conflict(prepared_theory, assigned_atoms);
#endif
    if (!conflict_clause.has_value()) {
      publish_sat_stats(backend.statistics(), statistics);
      if (!options.produce_model) {
        return CheckSatOutcome{CheckSatResult::Sat, "", statistics, ""};
      }
      const std::vector<AssignedTheoryAtom> assigned_atoms =
          assigned_atoms_from_backend(backend, clausifier.theory_atoms());
#if PRIMO_HAVE_IPASIR
      return CheckSatOutcome{
          CheckSatResult::Sat, "", statistics,
          build_qf_lra_model(terms, model_terms, preprocessing_model_assignments, prepared_theory,
                             online_propagator.tableau(), assigned_atoms)};
#else
      theory::arithmetic::LraTableau model_tableau = prepared_theory.base_tableau;
      for (std::size_t i = 0; i < assigned_atoms.size(); ++i) {
        const AssignedTheoryAtom& atom = assigned_atoms[i];
        const PreparedLraTheory::PreparedAtom& prepared_atom = prepared_theory.atoms[i];
        if (!prepared_atom.is_arithmetic || atom.filtered) {
          continue;
        }
        const auto conflict = assert_lra_atom_bound(
            model_tableau, prepared_atom.bound_var, prepared_atom.relation, prepared_atom.rhs,
            atom.value, prepared_atom.asserts_lower_when_true, blocking_lit(atom));
        if (conflict.has_value()) {
          return CheckSatOutcome{CheckSatResult::Unknown,
                                 "LRA model reconstruction found a bound conflict", statistics, ""};
        }
      }
      if (model_tableau.check().has_value()) {
        return CheckSatOutcome{CheckSatResult::Unknown,
                               "LRA model reconstruction found a tableau conflict", statistics, ""};
      }
      return CheckSatOutcome{CheckSatResult::Sat, "", statistics,
                             build_qf_lra_model(terms, model_terms, preprocessing_model_assignments,
                                                prepared_theory, model_tableau, assigned_atoms)};
#endif
    }
    statistics.increment(kStatLraTheoryConflicts);
    if (conflict_clause->empty()) {
      publish_sat_stats(backend.statistics(), statistics);
      return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
    }
    backend.add_clause(conflict_clause.value());
    publish_sat_stats(backend.statistics(), statistics);
  }
}

void record_disequality_propagation_opportunities(const core::TermManager& terms,
                                                  const PreparedFlatTheory& prepared,
                                                  const std::vector<AssignedTheoryAtom>& atoms,
                                                  util::Statistics& statistics) {
  if (atoms.size() != prepared.flat_atoms.size()) {
    throw UnsupportedInput("internal theory atom mapping mismatch");
  }

  std::unordered_map<TermPairKey, bool, TermPairKeyHash> source_equalities;
  std::unordered_map<FlatPairKey, bool, FlatPairKeyHash> flat_equalities;
  source_equalities.reserve(atoms.size());
  flat_equalities.reserve(atoms.size());

  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (atoms[i].filtered || !prepared.flat_atoms[i].equality) {
      continue;
    }
    const core::Term& term = terms.get(atoms[i].term);
    if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
      continue;
    }
    source_equalities.emplace(canonical_term_pair(term.children[0], term.children[1]),
                              atoms[i].value);
    flat_equalities.emplace(
        canonical_flat_pair(prepared.flat_atoms[i].lhs, prepared.flat_atoms[i].rhs),
        atoms[i].value);
  }

  std::unordered_map<theory::equality::FlatTermId, theory::equality::FlatDefinition>
      definition_by_result;
  definition_by_result.reserve(prepared.flat.definitions().size());
  for (const theory::equality::FlatDefinition& definition : prepared.flat.definitions()) {
    definition_by_result.emplace(definition.result, definition);
  }

  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (atoms[i].filtered || !prepared.flat_atoms[i].equality || atoms[i].value) {
      continue;
    }

    const auto lhs_definition = definition_by_result.find(prepared.flat_atoms[i].lhs);
    const auto rhs_definition = definition_by_result.find(prepared.flat_atoms[i].rhs);
    if (lhs_definition == definition_by_result.end() ||
        rhs_definition == definition_by_result.end()) {
      continue;
    }
    statistics.increment(kStatEufDiseqPropAssignedAppDisequalities);

    const core::Term& source = terms.get(atoms[i].term);
    if (source.kind == core::TermKind::Equality && source.children.size() == 2) {
      const core::Term& lhs_source = terms.get(source.children[0]);
      const core::Term& rhs_source = terms.get(source.children[1]);
      if (lhs_source.kind == core::TermKind::Application &&
          rhs_source.kind == core::TermKind::Application &&
          lhs_source.symbol == rhs_source.symbol && lhs_source.children.size() == 1 &&
          rhs_source.children.size() == 1) {
        statistics.increment(kStatEufDiseqPropSourceUnaryCandidates);
        const auto found = source_equalities.find(
            canonical_term_pair(lhs_source.children[0], rhs_source.children[0]));
        if (found != source_equalities.end()) {
          statistics.increment(kStatEufDiseqPropSourceUnaryExistingArgAtom);
          statistics.increment(found->second ? kStatEufDiseqPropSourceUnaryArgAtomTrue
                                             : kStatEufDiseqPropSourceUnaryArgAtomFalse);
        }
      }
    }

    const theory::equality::FlatDefinition& lhs = lhs_definition->second;
    const theory::equality::FlatDefinition& rhs = rhs_definition->second;
    const auto argument_equality =
        flat_equalities.find(canonical_flat_pair(lhs.argument, rhs.argument));
    if (lhs.function == rhs.function) {
      statistics.increment(kStatEufDiseqPropFlatSameFunctionCandidates);
      if (argument_equality != flat_equalities.end()) {
        statistics.increment(kStatEufDiseqPropFlatSameFunctionExistingArgAtom);
        statistics.increment(argument_equality->second
                                 ? kStatEufDiseqPropFlatSameFunctionArgAtomTrue
                                 : kStatEufDiseqPropFlatSameFunctionArgAtomFalse);
      }
    }

    const auto function_equality =
        flat_equalities.find(canonical_flat_pair(lhs.function, rhs.function));
    if (function_equality != flat_equalities.end() && function_equality->second &&
        argument_equality != flat_equalities.end()) {
      statistics.increment(kStatEufDiseqPropGuardedFunctionGuardCandidates);
    }
    if (argument_equality != flat_equalities.end() && argument_equality->second &&
        function_equality != flat_equalities.end()) {
      statistics.increment(kStatEufDiseqPropGuardedArgumentGuardCandidates);
    }
  }
}

CheckSatOutcome solve_qf_uf(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const CheckSatOptions& options, util::Statistics& statistics,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments) {
  statistics.increment(kStatSolverQfUf);
  sat::CadicalBackend backend(options.cadical_options);
  const ActiveSolveStatsGuard active_solve_stats_guard(backend, statistics);
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }
  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  statistics.set(kStatPureTheoryFilterEnabled,
                 pure_theory_filter_enabled(options.pure_theory_filter) ? 1 : 0);
  publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
  const EufPropagationMode euf_propagation =
      resolve_euf_propagation_mode(options, clausifier.theory_atoms().size(), statistics);
  CheckSatOptions resolved_options = options;
  resolved_options.euf_propagation = euf_propagation;
  TheoryPhaseMode theory_phase =
      resolve_theory_phase_mode(options.theory_phase, clausifier.theory_atoms(), statistics);
  if (options.theory_phase == TheoryPhaseMode::Auto && theory_phase == TheoryPhaseMode::Polarity) {
    statistics.increment(kStatSatPhaseAutoPolarity);
  }
  if ((options.theory_phase == TheoryPhaseMode::Negative ||
       options.theory_phase == TheoryPhaseMode::Auto) &&
      options.auto_basic_single_predicate_polarity && statistics.get(kStatAutoBasicSingle) != 0 &&
      statistics.get(kStatEufPredicateAtoms) != 0) {
    theory_phase = TheoryPhaseMode::Polarity;
    statistics.increment(kStatSatPhaseAutoBasicSinglePredicatePolarity);
  }
  if ((options.theory_phase == TheoryPhaseMode::Negative ||
       options.theory_phase == TheoryPhaseMode::Auto) &&
      options.auto_basic_multi_polarity && statistics.get(kStatAutoBasicMulti) != 0) {
    theory_phase = TheoryPhaseMode::Polarity;
    statistics.increment(kStatSatPhaseAutoBasicMultiPolarity);
  }
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    switch (theory_phase) {
      case TheoryPhaseMode::Negative:
        backend.phase(sat::Lit{-atom.lit.value});
        statistics.increment(kStatSatTheoryNegativePhase);
        break;
      case TheoryPhaseMode::Positive:
        backend.phase(atom.lit);
        break;
      case TheoryPhaseMode::Polarity:
        if (atom.occurs_negative && !atom.occurs_positive) {
          backend.phase(sat::Lit{-atom.lit.value});
          statistics.increment(kStatSatTheoryNegativePhase);
        } else if (atom.occurs_positive && !atom.occurs_negative) {
          backend.phase(atom.lit);
        }
        break;
      case TheoryPhaseMode::None:
        break;
      case TheoryPhaseMode::Auto:
        break;
    }
  }
  publish_sat_stats(backend.statistics(), statistics);

  const core::TermId true_term = terms.mk_bool(true);
  const core::TermId false_term = terms.mk_bool(false);
  std::unordered_set<std::string> learned_model_propagations;
  std::optional<PreparedFlatTheory> prepared_flat_theory;
  if (options.euf_engine == EufEngine::Incremental) {
    prepared_flat_theory.emplace(terms, clausifier.theory_atoms(), true_term, false_term,
                                 options.euf_explain_search, options.euf_proof_forest_diagnostics);
  }

#if PRIMO_HAVE_IPASIR
  std::optional<TheoryConflictTerminator> conflict_terminator;
  if (options.max_theory_conflicts.has_value()) {
    conflict_terminator.emplace(statistics, options.max_theory_conflicts.value());
    backend.connect_terminator(&conflict_terminator.value());
  }
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
                           statistics, ""};
  }
#endif

  while (true) {
    statistics.increment(kStatSatSolveCalls);
    const sat::SatResult sat_result = backend.solve();
    if (online_propagator.has_value()) {
      online_propagator->flush_incremental_stats();
    }
    publish_sat_stats(backend.statistics(), statistics);
    if ((conflict_terminator.has_value() && conflict_terminator->reached()) ||
        (options.max_theory_conflicts.has_value() &&
         statistics.get(kStatTheoryConflicts) >= options.max_theory_conflicts.value())) {
      return CheckSatOutcome{CheckSatResult::Unknown, "theory conflict limit reached", statistics,
                             ""};
    }
    if (sat_result != sat::SatResult::Sat) {
      return CheckSatOutcome{map_sat_result(sat_result), "", statistics, ""};
    }
    statistics.increment(kStatTheoryChecks);

    std::vector<AssignedTheoryAtom> assigned_atoms;
    for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      const std::optional<bool> assignment = backend.value(var);
      if (!assignment.has_value()) {
        return CheckSatOutcome{CheckSatResult::Unknown, "SAT backend did not assign theory atom",
                               statistics, ""};
      }
      const bool atom_value = atom.lit.value > 0 ? assignment.value() : !assignment.value();
      AssignedTheoryAtom assigned =
          assigned_theory_atom(options.pure_theory_filter, atom, atom_value);
      count_filtered_assignment(assigned, statistics);
      assigned_atoms.push_back(assigned);
    }
    if (resolved_options.euf_diseq_propagation_diagnostics && prepared_flat_theory.has_value()) {
      record_disequality_propagation_opportunities(terms, prepared_flat_theory.value(),
                                                   assigned_atoms, statistics);
    }
    if (resolved_options.euf_dynamic_ackermann_shadow) {
      std::vector<std::optional<bool>> atom_values;
      atom_values.reserve(assigned_atoms.size());
      for (const AssignedTheoryAtom& atom : assigned_atoms) {
        atom_values.push_back(atom.value);
      }
      record_dynamic_ackermann_shadow(terms, clausifier.theory_atoms(), atom_values, statistics);
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

    std::optional<sat::Clause> conflict_clause;
    if (options.euf_engine == EufEngine::Incremental && online_propagator.has_value()) {
      // The online/conflict SAT-callback propagator already owns a persistent
      // flat closure kept in sync with the trail; reuse its verdict instead of
      // rebuilding an equivalent closure from the full assignment below.
      conflict_clause = online_propagator->incremental_conflict();
    } else if (options.euf_engine == EufEngine::Incremental) {
      conflict_clause = find_incremental_theory_conflict(prepared_flat_theory.value(),
                                                         assigned_atoms, statistics);
    } else {
      conflict_clause = find_theory_conflict(terms, assigned_atoms, true_term, false_term);
    }
    if (!conflict_clause.has_value()) {
      publish_sat_stats(backend.statistics(), statistics);
      if (!options.produce_model) {
        return CheckSatOutcome{CheckSatResult::Sat, "", statistics, ""};
      }
      std::vector<TheoryModelAssignment> model_assignments;
      model_assignments.reserve(preprocessing_model_assignments.size() + assigned_atoms.size());
      append_model_assignments(model_assignments, preprocessing_model_assignments);
      for (const AssignedTheoryAtom& atom : assigned_atoms) {
        model_assignments.push_back(TheoryModelAssignment{atom.term, atom.value, atom.filtered});
      }
      return CheckSatOutcome{CheckSatResult::Sat, "", statistics,
                             build_qf_uf_model(terms, model_assignments)};
    }
    statistics.increment(kStatTheoryConflicts);
    if (options.max_theory_conflicts.has_value() &&
        statistics.get(kStatTheoryConflicts) >= options.max_theory_conflicts.value()) {
      publish_sat_stats(backend.statistics(), statistics);
      return CheckSatOutcome{CheckSatResult::Unknown, "theory conflict limit reached", statistics,
                             ""};
    }
    if (conflict_clause->empty()) {
      publish_sat_stats(backend.statistics(), statistics);
      return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
    }
    backend.add_clause(conflict_clause.value());
    publish_sat_stats(backend.statistics(), statistics);
  }
}

// Reports util::Rational's two-tier-representation counters (see that class's
// comment and docs/qf-lra-heuristics.md's H2) as :rational.promotions and
// :rational.big-operations. They are process-wide running totals, so this
// records the delta across one solve. Wrapping rather than publishing inline
// because solve_qf_lra_impl returns from a dozen places and CheckSatOutcome
// carries a *copy* of the statistics taken at the return statement -- an RAII
// guard would update the original after that copy was already made.
//
// Diagnostic only, but not optional: without them, "is the machine-word fast
// path actually being taken on this benchmark, and would a different small-form
// width take it more often" is a guess rather than a measurement.
CheckSatOutcome solve_qf_lra(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const std::vector<core::TermId>& model_terms,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments,
    const CheckSatOptions& options, util::Statistics& statistics) {
  const std::uint64_t promotions_before = util::Rational::promotion_count();
  const std::uint64_t big_operations_before = util::Rational::big_operation_count();
  CheckSatOutcome outcome = solve_qf_lra_impl(terms, assertions, model_terms,
                                              preprocessing_model_assignments, options, statistics);
  const std::uint64_t promotions = util::Rational::promotion_count() - promotions_before;
  const std::uint64_t big_operations =
      util::Rational::big_operation_count() - big_operations_before;
  statistics.increment(util::StatKey::RationalPromotions, promotions);
  statistics.increment(util::StatKey::RationalBigOperations, big_operations);
  outcome.statistics.increment(util::StatKey::RationalPromotions, promotions);
  outcome.statistics.increment(util::StatKey::RationalBigOperations, big_operations);
  return outcome;
}

CheckSatOutcome solve_qf_uf_lra(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const std::vector<core::TermId>& model_terms,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments,
    const CheckSatOptions& options, util::Statistics& statistics) {
  statistics.increment(":solver.qf-uflra");
  sat::CadicalBackend backend(options.cadical_options);
  const ActiveSolveStatsGuard active_solve_stats_guard(backend, statistics);
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }

  std::vector<BooleanClausifier::TheoryAtom> euf_atoms;
  std::vector<BooleanClausifier::TheoryAtom> lra_atoms;
  euf_atoms.reserve(clausifier.theory_atoms().size());
  lra_atoms.reserve(clausifier.theory_atoms().size());
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    require_supported_mixed_theory_atom(terms, atom);
    if (is_euf_theory_atom(terms, atom.term)) {
      euf_atoms.push_back(atom);
    } else if (is_lra_theory_atom(terms, atom.term)) {
      lra_atoms.push_back(atom);
    } else {
      throw UnsupportedInput("unsupported mixed-theory atom");
    }
  }
  const std::vector<core::TermId> shared_real_terms =
      shared_real_constants(terms, euf_atoms, lra_atoms);
  const std::vector<TermPairKey> shared_real_pairs = all_term_pairs(shared_real_terms);
  TheoryCombinationManager no_manager(terms);
  no_manager.rebuild_from_interface_terms(shared_real_terms);
  std::optional<sat::Lit> pending_model_equality_branch;
  std::unordered_set<int> tried_model_equality_vars;

  statistics.set(kStatTheoryAtoms, clausifier.theory_atoms().size());
  statistics.set(":no.mixed.euf-atoms", euf_atoms.size());
  statistics.set(":no.mixed.lra-atoms", lra_atoms.size());
  statistics.set(":no.interface.shared-real-terms", shared_real_terms.size());
  statistics.set(":no.interface.sat-equality-atoms",
                 registered_interface_equality_atom_count(no_manager));
  statistics.set(":no.interface.sat-equalities-true", 0);
  statistics.set(":no.interface.sat-equalities-false", 0);
  statistics.set(":no.interface.sat-equalities-forced", 0);
  statistics.set(":no.interface.equalities-propagated", 0);
  statistics.set(":no.interface.euf-to-lra-equalities", 0);
  statistics.set(":no.interface.lra-to-euf-equalities", 0);
  statistics.set(":no.interface.conflicts", 0);
  statistics.set(":no.interface.euf-conflicts", 0);
  statistics.set(":no.interface.lra-conflicts", 0);
  statistics.set(":no.interface.model-equality-candidates", 0);
  statistics.set(":no.interface.model-equality-created", 0);
  statistics.set(":no.interface.model-equality-assumptions", 0);
  statistics.set(":no.interface.model-equality-failed-assumptions", 0);
  statistics.set(":no.interface.model-equality-conflicts", 0);
  statistics.set(":no.interface.model-equality-full-pairs", 0);
  statistics.set(":no.interface.model-equality-care-pairs", 0);
  statistics.set(":no.interface.model-equality-care-pruned-pairs", 0);
  statistics.set(":no.interface.arrangement-checks", 0);
  statistics.set(":no.interface.arrangement-failures", 0);
  statistics.set(":no.interface.arrangement-separations", 0);
  statistics.set(":no.interface.euf-care-checks", 0);
  statistics.set(":no.interface.euf-care-full-pairs", 0);
  statistics.set(":no.interface.euf-care-pairs", 0);
  statistics.set(":no.interface.euf-care-pruned-pairs", 0);
  publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
  publish_sat_stats(backend.statistics(), statistics);

  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    if (options.theory_phase == TheoryPhaseMode::Negative ||
        options.theory_phase == TheoryPhaseMode::Auto) {
      backend.phase(sat::Lit{-atom.lit.value});
      statistics.increment(kStatSatTheoryNegativePhase);
    } else if (options.theory_phase == TheoryPhaseMode::Positive) {
      backend.phase(atom.lit);
    } else if (options.theory_phase == TheoryPhaseMode::Polarity) {
      if (atom.occurs_negative && !atom.occurs_positive) {
        backend.phase(sat::Lit{-atom.lit.value});
        statistics.increment(kStatSatTheoryNegativePhase);
      } else if (atom.occurs_positive && !atom.occurs_negative) {
        backend.phase(atom.lit);
      }
    }
  }

  const core::TermId true_term = terms.mk_bool(true);
  const core::TermId false_term = terms.mk_bool(false);
  {
    const PreparedLraTheory prepared_lra_theory(
        terms, lra_atoms, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
        options.lra_sparse_leaving, options.lra_fixed_elimination, options.produce_model);
    publish_lra_tableau_shape_stats(prepared_lra_theory.base_tableau, statistics);
  }

  while (true) {
    statistics.increment(kStatSatSolveCalls);
    std::optional<sat::Lit> active_model_equality_branch;
    if (pending_model_equality_branch.has_value()) {
      active_model_equality_branch = pending_model_equality_branch;
      pending_model_equality_branch.reset();
      backend.assume(active_model_equality_branch.value());
      statistics.increment(":no.interface.model-equality-assumptions");
    }
    const sat::SatResult sat_result = backend.solve();
    publish_sat_stats(backend.statistics(), statistics);
    if (sat_result == sat::SatResult::Unsat && active_model_equality_branch.has_value() &&
        backend.failed(active_model_equality_branch.value())) {
      backend.add_clause(sat::Clause{sat::Lit{-active_model_equality_branch->value}});
      statistics.increment(":no.interface.model-equality-failed-assumptions");
      publish_sat_stats(backend.statistics(), statistics);
      continue;
    }
    if (sat_result != sat::SatResult::Sat) {
      return CheckSatOutcome{map_sat_result(sat_result), "", statistics, ""};
    }

    const std::vector<InterfaceEqualityRegistryEntry> true_interface_equalities =
        collect_true_interface_equalities(backend, no_manager);
    const std::vector<InterfaceEqualityRegistryEntry> false_interface_equalities =
        collect_false_interface_equalities(backend, no_manager);
    statistics.set(":no.interface.sat-equalities-true", true_interface_equalities.size());
    statistics.set(":no.interface.sat-equalities-false", false_interface_equalities.size());

    std::optional<sat::Clause> euf_conflict;
    std::optional<EufClosureCheckResult> euf_check;
    std::vector<AssignedTheoryAtom> euf_assignments;
    std::vector<BooleanClausifier::TheoryAtom> euf_atoms_for_check = euf_atoms;
    std::vector<AssignedTheoryAtom> euf_assignments_for_check;
    std::optional<PreparedFlatTheory> prepared_flat_theory;
    EufCarePairDiagnostics euf_care;
    if (!euf_atoms.empty()) {
      statistics.increment(kStatTheoryChecks);
      euf_assignments = assigned_atoms_from_backend(backend, euf_atoms, options.pure_theory_filter);
      for (const AssignedTheoryAtom& atom : euf_assignments) {
        count_filtered_assignment(atom, statistics);
      }
      euf_assignments_for_check = euf_assignments;
      append_true_interface_equalities(true_interface_equalities, euf_atoms_for_check,
                                       euf_assignments_for_check);
      append_interface_equalities(false_interface_equalities, false, euf_atoms_for_check,
                                  euf_assignments_for_check);
      prepared_flat_theory.emplace(terms, euf_atoms_for_check, true_term, false_term,
                                   options.euf_explain_search,
                                   options.euf_proof_forest_diagnostics);
      euf_check = check_incremental_euf_closure(prepared_flat_theory.value(),
                                                euf_assignments_for_check, statistics);
      euf_care = euf_care_pair_diagnostics(prepared_flat_theory.value(), euf_check->closure,
                                           shared_real_terms);
      publish_euf_care_pair_diagnostics(euf_care, statistics);
      euf_conflict = euf_check->conflict;
    }
    if (euf_conflict.has_value()) {
      statistics.increment(kStatTheoryConflicts);
      record_interface_conflict(euf_conflict, no_manager, ":no.interface.euf-conflicts",
                                statistics);
      if (active_model_equality_branch.has_value()) {
        statistics.increment(":no.interface.model-equality-conflicts");
      }
      if (euf_conflict->empty()) {
        publish_sat_stats(backend.statistics(), statistics);
        return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
      }
      backend.add_clause(euf_conflict.value());
      publish_sat_stats(backend.statistics(), statistics);
      continue;
    }

    statistics.increment(kStatLraTheoryChecks);
    std::vector<BooleanClausifier::TheoryAtom> lra_atoms_for_check = lra_atoms;
    std::vector<AssignedTheoryAtom> lra_assignments =
        assigned_atoms_from_backend(backend, lra_atoms);
    std::vector<AssignedTheoryAtom> lra_assignments_for_check = lra_assignments;
    append_true_interface_equalities(true_interface_equalities, lra_atoms_for_check,
                                     lra_assignments_for_check);
    const PreparedLraTheory prepared_lra_theory_for_check(
        terms, lra_atoms_for_check, lra_pivoting_rule_from_options(options.lra_pivoting_rule),
        options.lra_sparse_leaving, options.lra_fixed_elimination, options.produce_model);
    std::optional<sat::Clause> lra_conflict = find_incremental_lra_theory_conflict(
        prepared_lra_theory_for_check, lra_assignments_for_check);
    if (lra_conflict.has_value()) {
      statistics.increment(kStatLraTheoryConflicts);
      record_interface_conflict(lra_conflict, no_manager, ":no.interface.lra-conflicts",
                                statistics);
      if (active_model_equality_branch.has_value()) {
        statistics.increment(":no.interface.model-equality-conflicts");
      }
      if (lra_conflict->empty()) {
        publish_sat_stats(backend.statistics(), statistics);
        return CheckSatOutcome{CheckSatResult::Unsat, "", statistics, ""};
      }
      backend.add_clause(lra_conflict.value());
      publish_sat_stats(backend.statistics(), statistics);
      continue;
    }

    if (euf_check.has_value() && !shared_real_terms.empty()) {
      const std::vector<core::TermId> implied_equalities = euf_implied_real_equalities(
          terms, prepared_flat_theory.value(), euf_check->closure, shared_real_terms);
      statistics.increment(":no.interface.equalities-propagated", implied_equalities.size());
      statistics.increment(":no.interface.euf-to-lra-equalities", implied_equalities.size());
      if (force_implied_interface_equalities(terms, clausifier, backend, no_manager,
                                             implied_equalities, euf_assignments_for_check,
                                             statistics)) {
        publish_sat_stats(backend.statistics(), statistics);
        continue;
      }
    }

    if (euf_check.has_value() && !shared_real_terms.empty()) {
      const std::vector<core::TermId> implied_equalities = lra_implied_real_equalities(
          terms, lra_atoms_for_check, lra_assignments_for_check, shared_real_terms, options);
      statistics.increment(":no.interface.equalities-propagated", implied_equalities.size());
      statistics.increment(":no.interface.lra-to-euf-equalities", implied_equalities.size());
      if (force_implied_interface_equalities(terms, clausifier, backend, no_manager,
                                             implied_equalities, lra_assignments_for_check,
                                             statistics)) {
        publish_sat_stats(backend.statistics(), statistics);
        continue;
      }
    }

    pending_model_equality_branch = choose_lra_model_equality_branch(
        terms, clausifier, backend, no_manager, lra_atoms_for_check, lra_assignments_for_check,
        shared_real_pairs, options, tried_model_equality_vars, statistics);
    if (pending_model_equality_branch.has_value()) {
      publish_sat_stats(backend.statistics(), statistics);
      continue;
    }

    publish_sat_stats(backend.statistics(), statistics);
    if (!options.produce_model && euf_check.has_value()) {
      const std::vector<core::TermId> euf_model_real_terms =
          real_constants_in_atoms(terms, euf_atoms);
      if (!euf_model_real_terms.empty()) {
        statistics.increment(":no.interface.arrangement-checks");
        const std::vector<core::TermId> euf_implied_equalities = euf_implied_real_equalities(
            terms, prepared_flat_theory.value(), euf_check->closure, euf_model_real_terms);
        std::vector<core::TermId> forced_model_equalities = euf_implied_equalities;
        if (!shared_real_terms.empty()) {
          const std::vector<core::TermId> lra_implied_equalities_for_euf =
              lra_implied_real_equalities(terms, lra_atoms_for_check, lra_assignments_for_check,
                                          shared_real_terms, options);
          forced_model_equalities.insert(forced_model_equalities.end(),
                                         lra_implied_equalities_for_euf.begin(),
                                         lra_implied_equalities_for_euf.end());
        }
        const std::optional<std::vector<core::TermId>> separation_atoms =
            shared_real_model_separation_atoms(
                terms, lra_atoms, lra_assignments, prepared_flat_theory.value(), euf_check->closure,
                euf_model_real_terms, forced_model_equalities, options, statistics);
        if (!separation_atoms.has_value()) {
          statistics.increment(":no.interface.arrangement-failures");
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA final arrangement could not separate Real argument constants",
              statistics, ""};
        }
        statistics.set(":no.interface.arrangement-separations", separation_atoms->size());
      }
    }
    if (options.produce_model) {
      std::vector<core::TermId> euf_implied_equalities;
      std::vector<core::TermId> lra_implied_equalities_for_euf;
      std::vector<core::TermId> extra_lra_model_atoms;
      const std::vector<core::TermId> euf_model_real_terms =
          euf_check.has_value() ? real_constants_in_atoms(terms, euf_atoms)
                                : std::vector<core::TermId>{};
      if (euf_check.has_value() && !euf_model_real_terms.empty()) {
        euf_implied_equalities = euf_implied_real_equalities(
            terms, prepared_flat_theory.value(), euf_check->closure, euf_model_real_terms);
        if (!shared_real_terms.empty()) {
          lra_implied_equalities_for_euf = lra_implied_real_equalities(
              terms, lra_atoms_for_check, lra_assignments_for_check, shared_real_terms, options);
        }
        extra_lra_model_atoms = euf_implied_equalities;
        std::vector<core::TermId> forced_model_equalities = euf_implied_equalities;
        forced_model_equalities.insert(forced_model_equalities.end(),
                                       lra_implied_equalities_for_euf.begin(),
                                       lra_implied_equalities_for_euf.end());
        const std::optional<std::vector<core::TermId>> separation_atoms =
            shared_real_model_separation_atoms(
                terms, lra_atoms, lra_assignments, prepared_flat_theory.value(), euf_check->closure,
                euf_model_real_terms, forced_model_equalities, options, statistics);
        if (!separation_atoms.has_value()) {
          return CheckSatOutcome{
              CheckSatResult::Unknown,
              "mixed QF_UFLRA model reconstruction could not separate Real argument constants",
              statistics, ""};
        }
        extra_lra_model_atoms.insert(extra_lra_model_atoms.end(), separation_atoms->begin(),
                                     separation_atoms->end());
        statistics.set(":no.interface.model-separations", separation_atoms->size());
      } else {
        statistics.set(":no.interface.model-separations", 0);
        statistics.set(":no.interface.model-separation-candidates", 0);
        statistics.set(":no.interface.model-separation-search-capped", 0);
        statistics.set(":no.interface.model-separation-search-branches", 0);
        statistics.set(":no.interface.model-separation-search-backtracks", 0);
      }

      std::optional<LraModelState> lra_model = build_lra_model_state(
          terms, lra_atoms_for_check, lra_assignments_for_check, extra_lra_model_atoms, options);
      if (!lra_model.has_value()) {
        return CheckSatOutcome{CheckSatResult::Unknown,
                               "mixed QF_UFLRA model reconstruction found an LRA conflict",
                               statistics, ""};
      }
      const RealModelValues real_values = real_model_values_from_lra(
          terms, model_terms, preprocessing_model_assignments, lra_model->prepared,
          lra_model->tableau, lra_model->assignments);
      std::vector<TheoryModelAssignment> uf_model_assignments;
      append_supported_uf_model_assignments(terms, uf_model_assignments,
                                            preprocessing_model_assignments);
      uf_model_assignments.reserve(uf_model_assignments.size() + euf_assignments.size() +
                                   lra_implied_equalities_for_euf.size());
      for (const AssignedTheoryAtom& atom : euf_assignments) {
        uf_model_assignments.push_back(TheoryModelAssignment{atom.term, atom.value, atom.filtered});
      }
      for (const core::TermId equality : lra_implied_equalities_for_euf) {
        uf_model_assignments.push_back(TheoryModelAssignment{equality, true, false});
      }
      std::ostringstream model;
      model << "(model\n";
      model << build_qf_uf_model(terms, uf_model_assignments,
                                 QfUfModelBuildOptions{.include_model_wrapper = false,
                                                       .skip_real_symbols = false,
                                                       .skip_real_constants = true,
                                                       .real_values = &real_values});
      append_real_constant_model_definitions(model, terms, model_terms, real_values);
      model << ")";
      return CheckSatOutcome{CheckSatResult::Sat, "", statistics, model.str()};
    }
    return CheckSatOutcome{CheckSatResult::Sat, "", statistics, ""};
  }
}

}  // namespace

void refresh_active_solve_stats() {
  if (g_active_backend != nullptr && g_active_backend_statistics != nullptr) {
    publish_sat_stats(g_active_backend->statistics(), *g_active_backend_statistics);
  }
}

CheckSatOutcome check_sat_smtlib_once(std::string_view input, CheckSatOptions options) {
  const PhaseClock::time_point total_start = PhaseClock::now();
  util::Statistics local_statistics;
  util::Statistics& statistics =
      options.live_statistics != nullptr ? *options.live_statistics : local_statistics;
  statistics = util::Statistics{};
  statistics.set(kStatTimeTotalUs, 0);
  statistics.set(kStatTimeParseUs, 0);
  statistics.set(kStatTimeFrontendUs, 0);
  statistics.set(kStatTimePreprocessingUs, 0);
  statistics.set(kStatTimeSolveUs, 0);
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
  statistics.set(kStatRealEqualityLowered, 0);
  statistics.set(kStatSolvedConstantGaussianCandidates, 0);
  statistics.set(kStatFiniteDomainDomains, 0);
  statistics.set(kStatFiniteDomainAmoClauses, 0);
  statistics.set(kStatFiniteDomainRawChoices, 0);
  statistics.set(kStatFiniteDomainRawAmoClauses, 0);
  statistics.set(kStatFiniteDomainPostAmoClauses, 0);
  statistics.set(kStatFiniteDomainDedupedAmoClauses, 0);
  statistics.set(kStatFiniteDomainEvaluatedEqualities, 0);
  statistics.set(kStatFiniteDomainEvaluationCutoffs, 0);
  statistics.set(kStatEqualityDefinitions, 0);
  statistics.set(kStatEqualityDefinitionCandidatePairs, 0);
  statistics.set(kStatEqualityDefinitionAcceptedPairs, 0);
  statistics.set(kStatEqualityDefinitionPredictedClauses, 0);
  statistics.set(kStatEqualityDefinitionRejectedDomainSize, 0);
  statistics.set(kStatEqualityDefinitionRejectedClauseLimit, 0);
  statistics.set(kStatGroundCongruenceClauses, 0);
  statistics.set(kStatGroundCongruenceSkippedGroups, 0);
  statistics.set(kStatUfSymmetryAcceptedSets, 0);
  statistics.set(kStatUfSymmetryValues, 0);
  statistics.set(kStatUfSymmetryTerms, 0);
  statistics.set(kStatUfSymmetryConstraints, 0);
  statistics.set(kStatUfSymmetryValuePrecedenceTerms, 0);
  statistics.set(kStatUfSymmetryValuePrecedenceConstraints, 0);
  statistics.set(kStatUfSymmetryRejectedNoninvariant, 0);
  statistics.set(kStatAutoSingleAssertion, 0);
  statistics.set(kStatAutoStrong, 0);
  statistics.set(kStatAutoBasicSingle, 0);
  statistics.set(kStatAutoBasicMulti, 0);
  statistics.set(kStatAutoLargeAssertionSkip, 0);
  statistics.set(kStatSatTheoryNegativePhase, 0);
  statistics.set(kStatSatPhaseAutoPolarity, 0);
  statistics.set(kStatSatPhaseAutoBasicSinglePredicatePolarity, 0);
  statistics.set(kStatSatPhaseAutoBasicMultiPolarity, 0);
  statistics.set(kStatSatVars, 0);
  statistics.set(kStatSatClauses, 0);
  statistics.set(kStatSatClauseLits, 0);
  statistics.set(kStatSatUnitClauses, 0);
  statistics.set(kStatSatBinaryClauses, 0);
  statistics.set(kStatSolverBoolean, 0);
  statistics.set(kStatSolverQfUf, 0);
  statistics.set(kStatSolverQfLra, 0);
  statistics.set(kStatSatSolveCalls, 0);
  statistics.set(kStatTheoryAtoms, 0);
  statistics.set(kStatEufPredicateAtoms, 0);
  statistics.set(kStatPureTheoryFilterEnabled, 0);
  statistics.set(kStatPureTheoryAtomsPositive, 0);
  statistics.set(kStatPureTheoryAtomsNegative, 0);
  statistics.set(kStatPureTheoryLiteralsFiltered, 0);
  statistics.set(kStatEufAssignments, 0);
  statistics.set(kStatEufEqAssignments, 0);
  statistics.set(kStatEufDiseqAssignments, 0);
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
  statistics.set(kStatEufDiseqPropAssignedAppDisequalities, 0);
  statistics.set(kStatEufDiseqPropSourceUnaryCandidates, 0);
  statistics.set(kStatEufDiseqPropSourceUnaryExistingArgAtom, 0);
  statistics.set(kStatEufDiseqPropSourceUnaryArgAtomTrue, 0);
  statistics.set(kStatEufDiseqPropSourceUnaryArgAtomFalse, 0);
  statistics.set(kStatEufDiseqPropFlatSameFunctionCandidates, 0);
  statistics.set(kStatEufDiseqPropFlatSameFunctionExistingArgAtom, 0);
  statistics.set(kStatEufDiseqPropFlatSameFunctionArgAtomTrue, 0);
  statistics.set(kStatEufDiseqPropFlatSameFunctionArgAtomFalse, 0);
  statistics.set(kStatEufDiseqPropGuardedFunctionGuardCandidates, 0);
  statistics.set(kStatEufDiseqPropGuardedArgumentGuardCandidates, 0);
  statistics.set(kStatEufAckermannApplications, 0);
  statistics.set(kStatEufAckermannFunctionSymbols, 0);
  statistics.set(kStatEufAckermannEligibleSymbols, 0);
  statistics.set(kStatEufAckermannUnarySymbols, 0);
  statistics.set(kStatEufAckermannNarySymbols, 0);
  statistics.set(kStatEufAckermannEstimatedConstraints, 0);
  statistics.set(kStatEufAckermannEstimatedEqualities, 0);
  statistics.set(kStatEufAckermannEstimatedClauseLits, 0);
  statistics.set(kStatEufAckermannMaxSymbolApplications, 0);
  statistics.set(kStatEufAckermannMaxSymbolArity, 0);
  statistics.set(kStatEufAckermannSymbolsCostLe100, 0);
  statistics.set(kStatEufAckermannSymbolsCostLe1000, 0);
  statistics.set(kStatEufAckermannSymbolsCostLe10000, 0);
  statistics.set(kStatEufDynamicAckShadowInvocations, 0);
  statistics.set(kStatEufDynamicAckShadowResultPairs, 0);
  statistics.set(kStatEufDynamicAckShadowArgAtomsMissing, 0);
  statistics.set(kStatEufDynamicAckShadowSatVisibleClauses, 0);
  statistics.set(kStatEufDynamicAckShadowSatisfied, 0);
  statistics.set(kStatEufDynamicAckShadowUnit, 0);
  statistics.set(kStatEufDynamicAckShadowConflicting, 0);
  statistics.set(kStatEufDynamicAckShadowResultDiseqAssigned, 0);
  statistics.set(kStatEufDynamicAckShadowAllArgsEqAssigned, 0);
  statistics.set(kStatEufDynamicAckShadowWidthTotal, 0);
  statistics.set(kStatEufDynamicAckShadowMaxWidth, 0);
  statistics.set(kStatEufDynamicAckShadowWidthLe3, 0);
  statistics.set(kStatEufDynamicAckShadowWidthGt3, 0);
  statistics.set(kStatEufDiseqAckCandidates, 0);
  statistics.set(kStatEufDiseqAckQueuePushes, 0);
  statistics.set(kStatEufDiseqAckQueuePops, 0);
  statistics.set(kStatEufDiseqAckWidthSkips, 0);
  statistics.set(kStatEufDiseqAckMissingArgAtom, 0);
  statistics.set(kStatEufDiseqAckSatisfiedSkips, 0);
  statistics.set(kStatEufDiseqAckUnit, 0);
  statistics.set(kStatEufDiseqAckConflict, 0);
  statistics.set(kStatEufDiseqAckPropagated, 0);
  statistics.set(kStatEufDiseqAckReturned, 0);
  statistics.set(kStatEufDiseqAckPendingCleared, 0);
  statistics.set(kStatEufDiseqAckReasonLits, 0);
  statistics.set(kStatEufDiseqAckQueueBudgetCutoffs, 0);
  statistics.set(kStatEufDiseqAckPerSymbolCutoffs, 0);
  statistics.set(kStatEufIncrementalTerms, 0);
  statistics.set(kStatEufIncrementalFlatDefinitions, 0);
  statistics.set(kStatEufIncrementalMerges, 0);
  statistics.set(kStatEufIncrementalPendingPops, 0);
  statistics.set(kStatEufIncrementalLookupProbes, 0);
  statistics.set(kStatEufIncrementalLookupHits, 0);
  statistics.set(kStatEufIncrementalCongruenceMerges, 0);
  statistics.set(kStatEufIncrementalDisequalityChecks, 0);
  statistics.set(kStatEufIncrementalConflicts, 0);
  statistics.set(kStatEufIncrementalExplainCalls, 0);
  statistics.set(kStatEufIncrementalExplainCacheHits, 0);
  statistics.set(kStatEufIncrementalExplainCacheBoundMisses, 0);
  statistics.set(kStatEufIncrementalExplainLength, 0);
  statistics.set(kStatEufIncrementalExplainTopLevelQueries, 0);
  statistics.set(kStatEufIncrementalExplainRecursiveQueries, 0);
  statistics.set(kStatEufIncrementalExplainRepeatedPairQueries, 0);
  statistics.set(kStatEufIncrementalExplainMaxRecursionDepth, 0);
  statistics.set(kStatEufIncrementalExplainBfsSearches, 0);
  statistics.set(kStatEufIncrementalExplainBfsVisited, 0);
  statistics.set(kStatEufIncrementalExplainBfsPathEdges, 0);
  statistics.set(kStatEufIncrementalExplainBfsMaxQueue, 0);
  statistics.set(kStatEufIncrementalExplainProofForestQueries, 0);
  statistics.set(kStatEufIncrementalExplainProofForestPathEdges, 0);
  statistics.set(kStatEufIncrementalExplainProofForestMaxPathEdges, 0);
  statistics.set(kStatEufIncrementalExplainProofForestMissingLca, 0);
  statistics.set(kStatEufIncrementalExplainProofForestShorterThanBfs, 0);
  statistics.set(kStatEufIncrementalExplainProofForestEqualToBfs, 0);
  statistics.set(kStatEufIncrementalExplainProofForestLongerThanBfs, 0);
  statistics.set(kStatEufIncrementalExplainCausalEdgeSkips, 0);
  statistics.set(kStatEufIncrementalExplainDirectEdges, 0);
  statistics.set(kStatEufIncrementalExplainCongruenceEdges, 0);
  statistics.set(kStatEufIncrementalExplainReasonDuplicateLiterals, 0);
  statistics.set(kStatEufIncrementalConflictReasonMaxLength, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe4, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe8, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe16, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe32, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthLe64, 0);
  statistics.set(kStatEufIncrementalConflictReasonLengthGt64, 0);
  statistics.set(kStatEufIncrementalBacktracks, 0);
  statistics.set(kStatEufIncrementalTrailEntries, 0);
  statistics.set(kStatEufIncrementalClassMemberTrailEntriesSaved, 0);
  statistics.set(kStatEufIncrementalUseListAppends, 0);
  statistics.set(kStatEufIncrementalUseListDuplicateSkips, 0);
  statistics.set(kStatEufIncrementalAffectedUseListDedup, 0);
  statistics.set(kStatEufIncrementalMaxUseListSize, 0);

  const auto finish = [&](CheckSatResult result, std::string message = "") {
    statistics.set(kStatTimeTotalUs, elapsed_microseconds(total_start, PhaseClock::now()));
    return CheckSatOutcome{result, std::move(message), statistics, ""};
  };

  try {
    const PhaseClock::time_point parse_start = PhaseClock::now();
    smtlib::Parser parser(input);
    const std::vector<smtlib::Command> commands = parser.parse_commands();
    statistics.set(kStatTimeParseUs, elapsed_microseconds(parse_start, PhaseClock::now()));

    const PhaseClock::time_point frontend_start = PhaseClock::now();
    Frontend frontend;
    std::vector<core::TermId> assertions;
    for (const smtlib::Command& command : commands) {
      frontend.process_command(command, assertions);
    }
    statistics.set(kStatTimeFrontendUs, elapsed_microseconds(frontend_start, PhaseClock::now()));
    statistics.set(kStatAssertionsInitial, assertions.size());
    statistics.set(kStatFrontendBooleanItes, frontend.boolean_ites());
    statistics.set(kStatFrontendAuxiliaryItes, frontend.auxiliary_ites());
    if (!frontend.saw_check_sat()) {
      return finish(CheckSatResult::Unsupported, "missing check-sat command");
    }
    if (frontend.requested_model()) {
      if (!frontend.produce_models()) {
        return finish(CheckSatResult::Unsupported, "get-model requires :produce-models true");
      }
      options.produce_model = true;
    }
    const PhaseClock::time_point preprocessing_start = PhaseClock::now();
    const std::size_t initial_reachable_terms =
        options.qf_uf_preprocess == QfUfPreprocessMode::Auto
            ? reachable_term_count(frontend.terms(), assertions)
            : 0;
    const bool auto_candidate_single_assertion =
        options.qf_uf_preprocess == QfUfPreprocessMode::Auto && assertions.size() <= 1;
    const bool auto_large_assertion_skip = options.qf_uf_preprocess == QfUfPreprocessMode::Auto &&
                                           assertions.size() > kAutoPreprocessLargeAssertionLimit;
    const bool auto_basic_multi = options.qf_uf_preprocess == QfUfPreprocessMode::Auto &&
                                  options.auto_basic_multi_preprocessing && assertions.size() > 1 &&
                                  assertions.size() <= kAutoPreprocessLargeAssertionLimit &&
                                  initial_reachable_terms <= kAutoBasicMultiTermLimit;
    const bool preprocessing_enabled =
        options.qf_uf_preprocess != QfUfPreprocessMode::Off && !auto_large_assertion_skip;
    const bool use_symmetry_breaking = preprocessing_enabled && options.uf_symmetry_breaking;
    const bool auto_basic_single_candidate =
        options.auto_basic_single_preprocessing && auto_candidate_single_assertion &&
        initial_reachable_terms <= kAutoBasicSingleTermLimit && use_symmetry_breaking;
    FiniteDomainAtMostOneResult raw_finite_domain_result;
    if (auto_candidate_single_assertion || options.qf_uf_preprocess == QfUfPreprocessMode::Strong ||
        (options.qf_uf_preprocess == QfUfPreprocessMode::Auto && assertions.size() > 1 &&
         !auto_basic_multi && !auto_large_assertion_skip)) {
      FiniteDomainAtMostOne finite_domain(frontend.terms());
      raw_finite_domain_result = finite_domain.generate(assertions);
    }
    std::optional<UfSymmetryBreakingResult> symmetry_result;
    if (auto_basic_single_candidate) {
      UfSymmetryBreaker symmetry_breaker(frontend.terms());
      symmetry_result = symmetry_breaker.break_symmetries(
          assertions,
          UfSymmetryBreakingOptions{.value_precedence = options.finite_domain_value_precedence});
    }
    const bool auto_basic_single =
        auto_basic_single_candidate && symmetry_result.has_value() &&
        should_use_auto_basic_single(assertions.size(), initial_reachable_terms,
                                     raw_finite_domain_result, symmetry_result.value());
    const bool auto_single_assertion = auto_candidate_single_assertion && !auto_basic_single;
    const bool auto_strong = options.qf_uf_preprocess == QfUfPreprocessMode::Auto &&
                             assertions.size() > 1 && !auto_large_assertion_skip &&
                             !auto_basic_multi;
    const bool auto_single_assertion_eqdefs_allowed =
        auto_single_assertion &&
        (options.finite_domain_eqdefs_auto_term_limit == 0 ||
         initial_reachable_terms <= options.finite_domain_eqdefs_auto_term_limit);
    if (auto_single_assertion) {
      statistics.increment(kStatAutoSingleAssertion);
    }
    if (auto_strong) {
      statistics.increment(kStatAutoStrong);
    }
    if (auto_basic_single) {
      statistics.increment(kStatAutoBasicSingle);
    }
    if (auto_basic_multi) {
      statistics.increment(kStatAutoBasicMulti);
    }
    if (auto_large_assertion_skip) {
      statistics.increment(kStatAutoLargeAssertionSkip);
    }
    const bool strong_preprocessing =
        options.qf_uf_preprocess == QfUfPreprocessMode::Strong || auto_strong;
    const bool use_eq_bridge = options.eq_bridge || strong_preprocessing || auto_single_assertion;
    const bool use_finite_domain_evaluation = false;
    const bool use_finite_domain_amo = strong_preprocessing || auto_single_assertion;
    const bool use_bounded_auto_equality_definitions =
        options.finite_domain_equality_definitions &&
        options.auto_finite_domain_equality_definitions &&
        options.qf_uf_preprocess == QfUfPreprocessMode::Auto &&
        (auto_strong || auto_single_assertion_eqdefs_allowed);
    const bool use_finite_domain_equality_definitions =
        options.finite_domain_equality_definitions &&
        (options.qf_uf_preprocess == QfUfPreprocessMode::Strong ||
         use_bounded_auto_equality_definitions);
    const bool use_ground_congruence_definitions = options.ground_congruence;
    const bool use_ground_unit_substitution = options.ground_unit_substitution;
    const bool use_guarded_ground_unit_substitution =
        options.guarded_ground_unit_substitution && !options.ground_unit_substitution;
    const std::size_t preprocessing_passes =
        (strong_preprocessing || auto_single_assertion)
            ? std::max<std::size_t>(options.preprocessing_passes, 3)
            : options.preprocessing_passes;
    const std::size_t adaptive_shrink_percent =
        std::min<std::size_t>(options.preprocessing_adaptive_shrink_percent, 100);
    std::size_t preprocessing_pass_limit = preprocessing_passes;

    statistics.set(kStatPreprocessingTermSizeInitial,
                   reachable_term_count(frontend.terms(), assertions));
    const std::vector<core::TermId> symmetry_source_assertions = assertions;
    std::vector<TheoryModelAssignment> preprocessing_model_assignments;
    std::vector<std::pair<core::TermId, core::TermId>> forced_equalities;
    std::vector<std::pair<core::TermId, core::TermId>> forced_disequalities;
    if (use_finite_domain_amo) {
      statistics.increment(kStatFiniteDomainRawChoices, raw_finite_domain_result.choices);
      statistics.increment(kStatFiniteDomainRawAmoClauses, raw_finite_domain_result.clauses.size());
    } else if (auto_basic_single) {
      statistics.increment(kStatFiniteDomainRawChoices, raw_finite_domain_result.choices);
      statistics.increment(kStatFiniteDomainRawAmoClauses, raw_finite_domain_result.clauses.size());
    }
    if (auto_large_assertion_skip && options.solved_constant_substitution) {
      SolvedConstantSubstitutionResult solved_constants =
          substitute_solved_constants(frontend.terms(), assertions, options.gaussian_elimination);
      assertions = std::move(solved_constants.assertions);
      if (options.produce_model) {
        append_equality_model_assignments(frontend.terms(), preprocessing_model_assignments,
                                          solved_constants.substitutions);
      }
      statistics.increment(kStatSolvedConstantCandidates, solved_constants.candidates);
      statistics.increment(kStatSolvedConstantAccepted, solved_constants.accepted);
      statistics.increment(kStatSolvedConstantRewrites, solved_constants.rewrites);
      statistics.increment(kStatSolvedConstantRejectedCycles, solved_constants.rejected_cycles);
      statistics.increment(kStatSolvedConstantRejectedBlowups, solved_constants.rejected_blowups);
      statistics.increment(kStatSolvedConstantGaussianCandidates,
                           solved_constants.gaussian_candidates);
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
      if (options.produce_model) {
        append_boolean_model_assignments(preprocessing_model_assignments,
                                         boolean_units.assignments);
      }
      statistics.increment(kStatBooleanUnitFacts, boolean_units.units);
      statistics.increment(kStatBooleanUnitRewrites, boolean_units.rewrites);
      ForcedAtomPropagationResult forced_atoms =
          propagate_forced_atoms(frontend.terms(), assertions);
      assertions = std::move(forced_atoms.assertions);
      if (options.produce_model) {
        append_boolean_model_assignments(preprocessing_model_assignments, forced_atoms.assignments);
      }
      statistics.increment(kStatForcedAtomFacts, forced_atoms.units);
      statistics.increment(kStatForcedAtomDisequalities, forced_atoms.disequalities);
      statistics.increment(kStatForcedAtomRewrites, forced_atoms.rewrites);
      forced_equalities.insert(forced_equalities.end(), forced_atoms.equality_pairs.begin(),
                               forced_atoms.equality_pairs.end());
      forced_disequalities.insert(forced_disequalities.end(),
                                  forced_atoms.disequality_pairs.begin(),
                                  forced_atoms.disequality_pairs.end());
      if (options.solved_constant_substitution) {
        SolvedConstantSubstitutionResult solved_constants =
            substitute_solved_constants(frontend.terms(), assertions, options.gaussian_elimination);
        assertions = std::move(solved_constants.assertions);
        if (options.produce_model) {
          append_equality_model_assignments(frontend.terms(), preprocessing_model_assignments,
                                            solved_constants.substitutions);
        }
        statistics.increment(kStatSolvedConstantCandidates, solved_constants.candidates);
        statistics.increment(kStatSolvedConstantAccepted, solved_constants.accepted);
        statistics.increment(kStatSolvedConstantRewrites, solved_constants.rewrites);
        statistics.increment(kStatSolvedConstantRejectedCycles, solved_constants.rejected_cycles);
        statistics.increment(kStatSolvedConstantRejectedBlowups, solved_constants.rejected_blowups);
        statistics.increment(kStatSolvedConstantGaussianCandidates,
                             solved_constants.gaussian_candidates);
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
          if (options.produce_model) {
            append_equality_model_assignments(frontend.terms(), preprocessing_model_assignments,
                                              ground_substitution.substitutions);
          }
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
    {
      // Runs once, after every pass above that reasons about genuine
      // Equality nodes (solve-eqs, eq-bridging, forced-atom/unit-equality
      // propagation), so Real equalities are visible to all of them. Must
      // still land before symmetry breaking/finite-domain clause
      // generation below and the has_equality/has_arithmetic dispatch, so
      // downstream passes/the tableau see the same Le/Lt-pair shape they
      // always have.
      RealEqualityLoweringResult lowered_equalities =
          lower_real_equalities(frontend.terms(), assertions);
      assertions = std::move(lowered_equalities.assertions);
      statistics.increment(kStatRealEqualityLowered, lowered_equalities.lowered);
    }
    if (use_symmetry_breaking) {
      if (!symmetry_result.has_value()) {
        UfSymmetryBreaker symmetry_breaker(frontend.terms());
        symmetry_result = symmetry_breaker.break_symmetries(
            symmetry_source_assertions,
            UfSymmetryBreakingOptions{.value_precedence = options.finite_domain_value_precedence});
      }
      const UfSymmetryBreakingResult& symmetry = symmetry_result.value();
      assertions.insert(assertions.end(), symmetry.constraints.begin(), symmetry.constraints.end());
      statistics.increment(kStatUfSymmetryAcceptedSets, symmetry.accepted_sets);
      statistics.increment(kStatUfSymmetryValues, symmetry.values);
      statistics.increment(kStatUfSymmetryTerms, symmetry.terms);
      statistics.increment(kStatUfSymmetryConstraints, symmetry.constraints.size());
      statistics.increment(kStatUfSymmetryValuePrecedenceTerms, symmetry.value_precedence_terms);
      statistics.increment(kStatUfSymmetryValuePrecedenceConstraints,
                           symmetry.value_precedence_constraints);
      statistics.increment(kStatUfSymmetryRejectedNoninvariant, symmetry.rejected_noninvariant);
    }
    if (use_finite_domain_amo) {
      FiniteDomainAtMostOne finite_domain(frontend.terms());
      FiniteDomainAtMostOneResult finite_domain_result;
      if (options.finite_domain_post_amo) {
        finite_domain_result =
            finite_domain.generate(assertions, forced_equalities, forced_disequalities);
      }
      statistics.increment(kStatFiniteDomainPostAmoClauses, finite_domain_result.clauses.size());
      std::unordered_set<core::TermId> emitted(assertions.begin(), assertions.end());
      std::size_t added = 0;
      std::size_t deduped = 0;
      for (const core::TermId clause : raw_finite_domain_result.clauses) {
        if (emitted.insert(clause).second) {
          assertions.push_back(clause);
          ++added;
        } else {
          ++deduped;
        }
      }
      for (const core::TermId clause : finite_domain_result.clauses) {
        if (emitted.insert(clause).second) {
          assertions.push_back(clause);
          ++added;
        } else {
          ++deduped;
        }
      }
      statistics.increment(kStatFiniteDomainDomains, std::max(raw_finite_domain_result.domains,
                                                              finite_domain_result.domains));
      statistics.increment(kStatFiniteDomainAmoClauses, added);
      statistics.increment(kStatFiniteDomainDedupedAmoClauses, deduped);
    }
    if (use_finite_domain_equality_definitions) {
      FiniteDomainEqualityDefinitions equality_definitions(frontend.terms());
      FiniteDomainEqualityDefinitionsOptions definition_options;
      if (use_bounded_auto_equality_definitions) {
        definition_options.max_domain_size = options.finite_domain_eqdefs_auto_domain_limit;
        definition_options.max_clauses = options.finite_domain_eqdefs_auto_clause_limit;
      }
      FiniteDomainEqualityDefinitionsResult equality_definition_result =
          equality_definitions.generate(assertions, definition_options);
      assertions.insert(assertions.end(), equality_definition_result.clauses.begin(),
                        equality_definition_result.clauses.end());
      statistics.increment(kStatEqualityDefinitions, equality_definition_result.clauses.size());
      statistics.increment(kStatEqualityDefinitionCandidatePairs,
                           equality_definition_result.candidate_term_pairs);
      statistics.increment(kStatEqualityDefinitionAcceptedPairs,
                           equality_definition_result.term_pairs);
      statistics.increment(kStatEqualityDefinitionPredictedClauses,
                           equality_definition_result.predicted_clauses);
      statistics.increment(kStatEqualityDefinitionRejectedDomainSize,
                           equality_definition_result.rejected_domain_size);
      statistics.increment(kStatEqualityDefinitionRejectedClauseLimit,
                           equality_definition_result.rejected_clause_limit);
    }
    {
      MixedTheoryPurificationResult purification =
          purify_mixed_arithmetic_applications(frontend, assertions);
      assertions = std::move(purification.assertions);
      statistics.set(":no.purification.definitions", purification.arithmetic_uf_definitions +
                                                         purification.euf_real_term_definitions);
      statistics.set(":no.purification.arithmetic-uf-definitions",
                     purification.arithmetic_uf_definitions);
      statistics.set(":no.purification.euf-real-term-definitions",
                     purification.euf_real_term_definitions);
      statistics.set(":no.purification.reused-applications", purification.reused_applications);
      statistics.set(":no.purification.reused-real-terms", purification.reused_real_terms);
    }
    statistics.set(kStatAssertionsFinal, assertions.size());
    statistics.set(kStatPreprocessingTermSizeFinal,
                   reachable_term_count(frontend.terms(), assertions));
    if (options.nelson_oppen_diagnostics) {
      TheoryCombinationManager no_manager(frontend.terms());
      no_manager.rebuild(assertions);
      const NelsonOppenInterfaceSummary& no_interface = no_manager.summary();
      statistics.set(":no.interface.terms", no_interface.terms.size());
      statistics.set(":no.interface.sorts", no_interface.sort_count);
      statistics.set(":no.interface.candidate-pairs", no_interface.candidate_pairs);
      statistics.set(":no.interface.registry-entries", no_interface.equality_registry.size());
      statistics.set(":no.interface.visible-equalities", no_interface.visible_equalities.size());
      statistics.set(":no.interface.missing-visible-equalities", no_interface.missing_equalities);
      statistics.set(":no.purification.sites", no_interface.purification_sites.size());
      statistics.set(":no.purification.euf-real-terms", no_interface.euf_real_terms);
      statistics.set(":no.purification.arithmetic-uf-apps",
                     no_interface.arithmetic_uninterpreted_applications);
    }
    if (options.euf_ackermann_diagnostics) {
      record_ackermann_cost_diagnostics(frontend.terms(), assertions, statistics);
    }
    for (const core::TermId assertion : assertions) {
      const core::Term& node = frontend.terms().get(assertion);
      if (node.kind == core::TermKind::BoolValue && !node.bool_value) {
        statistics.set(kStatTimePreprocessingUs,
                       elapsed_microseconds(preprocessing_start, PhaseClock::now()));
        return finish(CheckSatResult::Unsat);
      }
    }
    if (assertions.empty() && !options.preprocess_only) {
      statistics.set(kStatTimePreprocessingUs,
                     elapsed_microseconds(preprocessing_start, PhaseClock::now()));
      CheckSatOutcome outcome = finish(CheckSatResult::Sat);
      if (options.produce_model) {
        if (!frontend.declared_real_constants().empty()) {
          outcome.model = build_qf_lra_preprocessing_only_model(frontend.terms(),
                                                                frontend.declared_real_constants(),
                                                                preprocessing_model_assignments);
        } else {
          outcome.model = build_qf_uf_model(frontend.terms(), preprocessing_model_assignments);
        }
      }
      return outcome;
    }
    if (options.preprocess_only) {
      statistics.set(kStatTimePreprocessingUs,
                     elapsed_microseconds(preprocessing_start, PhaseClock::now()));
      return finish(CheckSatResult::Unknown, "preprocess-only");
    }

    CheckSatOptions solve_options = options;
    const BooleanShape final_boolean_shape = boolean_shape(frontend.terms(), assertions);
    statistics.set(":preprocessing.boolean-shape.or-nodes", final_boolean_shape.or_nodes);
    statistics.set(":preprocessing.boolean-shape.not-nodes", final_boolean_shape.not_nodes);
    statistics.set(":preprocessing.boolean-shape.top-level-or-nodes",
                   final_boolean_shape.top_level_or_nodes);
    statistics.set(":preprocessing.boolean-shape.max-top-level-or-children",
                   final_boolean_shape.max_top_level_or_children);
    if (should_use_auto_top_level_or_tseitin(options, raw_finite_domain_result,
                                             final_boolean_shape)) {
      solve_options.top_level_or_tseitin = true;
      statistics.increment(":sat.top-level-or-tseitin.auto");
    }
    statistics.set(kStatTimePreprocessingUs,
                   elapsed_microseconds(preprocessing_start, PhaseClock::now()));

    const bool has_equality = [&] {
      for (const core::TermId assertion : assertions) {
        if (contains_equality(frontend.terms(), assertion)) {
          return true;
        }
      }
      return false;
    }();
    const auto finish_solve = [&](CheckSatOutcome outcome, PhaseClock::time_point solve_start) {
      statistics.set(kStatTimeSolveUs, elapsed_microseconds(solve_start, PhaseClock::now()));
      statistics.set(kStatTimeTotalUs, elapsed_microseconds(total_start, PhaseClock::now()));
      outcome.statistics = statistics;
      return outcome;
    };
    const bool has_arithmetic = [&] {
      for (const core::TermId assertion : assertions) {
        if (contains_arithmetic_atom(frontend.terms(), assertion)) {
          return true;
        }
      }
      return false;
    }();
    const bool has_euf_predicate = [&] {
      for (const core::TermId assertion : assertions) {
        if (contains_euf_predicate_atom(frontend.terms(), assertion)) {
          return true;
        }
      }
      return false;
    }();
    if ((has_equality || has_euf_predicate) && has_arithmetic) {
      const PhaseClock::time_point solve_start = PhaseClock::now();
      return finish_solve(
          solve_qf_uf_lra(frontend.terms(), assertions, frontend.declared_real_constants(),
                          preprocessing_model_assignments, solve_options, statistics),
          solve_start);
    }
    if (has_arithmetic) {
      const PhaseClock::time_point solve_start = PhaseClock::now();
      return finish_solve(
          solve_qf_lra(frontend.terms(), assertions, frontend.declared_real_constants(),
                       preprocessing_model_assignments, solve_options, statistics),
          solve_start);
    }
    if (has_equality) {
      const PhaseClock::time_point solve_start = PhaseClock::now();
      return finish_solve(solve_qf_uf(frontend.terms(), assertions, solve_options, statistics,
                                      preprocessing_model_assignments),
                          solve_start);
    }
    try {
      const PhaseClock::time_point solve_start = PhaseClock::now();
      return finish_solve(solve_boolean_conjunction(frontend.terms(), assertions, solve_options,
                                                    statistics, preprocessing_model_assignments),
                          solve_start);
    } catch (const UnsupportedInput&) {
      const PhaseClock::time_point solve_start = PhaseClock::now();
      return finish_solve(solve_qf_uf(frontend.terms(), assertions, solve_options, statistics,
                                      preprocessing_model_assignments),
                          solve_start);
    }
  } catch (const UnsupportedInput& error) {
    return finish(CheckSatResult::Unsupported, error.what());
  } catch (const smtlib::ParseError& error) {
    return finish(CheckSatResult::Unsupported, error.what());
  } catch (const std::exception& error) {
    return finish(CheckSatResult::Unsupported, error.what());
  }
}

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options) {
  if (options.qf_uf_portfolio == QfUfPortfolioMode::Off) {
    return check_sat_smtlib_once(input, options);
  }

  CheckSatOptions first_options = options;
  first_options.qf_uf_portfolio = QfUfPortfolioMode::Off;
  first_options.qf_uf_preprocess = QfUfPreprocessMode::Basic;
  first_options.theory_phase = TheoryPhaseMode::Polarity;
  first_options.max_theory_conflicts = options.qf_uf_portfolio_conflicts;
  CheckSatOutcome first = check_sat_smtlib_once(input, first_options);
  first.statistics.set(":portfolio.qf-uf.attempts", 1);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.attempted", 1);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.conflict-budget",
                       options.qf_uf_portfolio_conflicts);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.first-conflicts",
                       first.statistics.get(kStatTheoryConflicts));
  if (!(first.result == CheckSatResult::Unknown &&
        first.message == "theory conflict limit reached")) {
    first.statistics.set(":portfolio.qf-uf.basic-polarity.solved", 1);
    return first;
  }

  CheckSatOptions fallback_options = options;
  fallback_options.qf_uf_portfolio = QfUfPortfolioMode::Off;
  CheckSatOutcome fallback = check_sat_smtlib_once(input, fallback_options);
  fallback.statistics.set(":portfolio.qf-uf.attempts", 2);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.attempted", 1);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.fell-back", 1);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.conflict-budget",
                          options.qf_uf_portfolio_conflicts);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.first-conflicts",
                          first.statistics.get(kStatTheoryConflicts));
  return fallback;
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
