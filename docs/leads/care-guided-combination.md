# Reducing combination obligations with care graphs

**Status: Open lead. Owner: unassigned.**

Investigate when care information can justify leaving some shared equality
questions unresolved. The required design and correctness argument are open;
implementation has not started. This is a larger change than
[refining care-pair generation](care-pair-generation.md), which should be tried
first and retains the current acceptance checks.

## Current implementation and paper basis

Primo already has a general component driver and care-first decision ordering.
Its {source}`care-pair API <src/theory/theory_component.hpp#L123-L125>` accepts
heuristic suggestions. The
{source}`model coordinator <src/smt/component_model_coordinator.cpp#L347-L423>`
still compares complete shared partitions when care pairs reveal no disagreement.
An empty care list currently supplies no guarantee that arbitrary remaining
equality choices will preserve satisfiability.

Dejan Jovanović and Clark Barrett, [*Being Careful about Theory Combination*](https://theory.stanford.edu/~barrett/pubs/JB13.pdf),
Formal Methods in System Design 42(1), 2013, pp. 67–90, supplies the proposed
basis: §3, Definitions 6–8; §3.1, the Arrange fixed point and Theorem 1; and
§4, the EUF construction. The earlier version is
[*Sharing is Caring: Combination of Theories*](https://cvc4.github.io/publications/2011/JB11-FroCoS.pdf),
FroCoS 2011, pp. 195–210.

The paper couples a care function to an equality propagator. For a satisfiable
component, an empty care graph must guarantee that every complete arrangement
extending the propagated equalities and disequalities remains satisfiable.
Its algorithm repeatedly exchanges consequences and adds newly required care
edges until a fixed point is reached. The correctness argument then establishes
that compatible models exist despite leaving other pairs out of the search.
Primo's current heuristic does not establish this contract.

## Design and proof obligations

- **Component contract.** Map the propagator's soundness, monotonicity, and
  inclusion of asserted interface relations to component operations. Define
  how a component promises the care-function guarantee and how the SMT layer
  distinguishes that promise from ordinary heuristic suggestions.
- **Changing state and progress.** Specify the care/propagation fixed point,
  explanations, and lifetime of its result. Include new congruence merges,
  disequalities, SAT rollback, newly registered atoms, and model mutation.
  Prove that a finite interface cannot produce an endless sequence of
  unproductive care choices; preserve typed inconclusive results and fallback.
- **Several components and sorts.** The cited theorem combines two theories.
  Map it to Primo's
  {source}`normalized scopes and admission conditions <src/smt/theory_combination_plan.cpp#L50-L181>`
  before claiming support for an entire component collection. Explain the role
  of a leading component and overlapping interfaces. Start with an explicitly
  delimited two-component case if necessary; retain the existing route for
  combinations outside the proved scope.
- **Constructing the model.** A proof that compatible models exist does not
  make the currently exported models compatible. Specify how components
  complete or revise their models, preserve concrete arithmetic values and
  function interpretations, and produce fresh tokens accepted by
  {source}`fragment validation and assembly <src/smt/component_model_assembler.cpp#L425-L582>`.
  Integer-search limits must still produce an inconclusive result when needed.

Simply deleting the complete partition check is not an implementation of this
lead. Keep that check and the existing fallback until the replacement's
acceptance condition and model-construction procedure have been justified.

## First investigation and acceptance

Write a local design checklist mapping the paper's operations and proof
obligations to Primo types. Use small independently enumerated partitions to
test the proposed contract, including empty care graphs, newly exposed edges,
non-convex alternatives, rollback, and disconnected sorts. Extend the existing
{source}`three/four-component partition-oracle tests <tests/unit/component_combination_tests.cpp#L409-L487>`
before admitting those configurations to the new procedure.

A prototype must pass targeted regressions, the applicable fresh
[fuzzing gates](../development/correctness.md), and independent validation of
SAT models. Compare it with the unchanged complete-comparison baseline using
paired measurements of equality atoms, theory checks, SAT calls, fallback
frequency, memory, solved count, runtime, and PAR2. Also compare against the
cheaper heuristic-only refinement so gains are attributed to the right change.

Accept only a documented, qualified subset. Defer or reject the approach if
model completion or maintaining the contract costs more than the avoided work,
or if the correctness argument requires guarantees the intended components
cannot provide. Preserve the design result and measurements even if no change
is promoted.
