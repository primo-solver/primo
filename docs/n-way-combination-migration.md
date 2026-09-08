# N-component combination migration

Status: implementation and QF_UFLIA qualification are complete; QF_UF and
QF_UFLRA also pass timing/model preservation, while QF_LRA and QF_LIA are running.
The production QF_UFLIA route uses
the collection driver, dispatcher and fragment assembler. Generated and
real-seed campaigns found no correctness failures, with explicitly inconclusive
resource rows retained below. The sample, watchlist and full 659-input paired
QF_UFLIA gates pass, including independent replay of every candidate SAT model.
The migration is **not yet fully qualified**: cross-logic preservation remains
an acceptance requirement. The frozen external binary remains the baseline.

## Scope and frozen boundary

The accepted objective is an extensible N-component combination architecture,
including production QF_UFLIA dispatch, incremental execution and model export.
Independent test components demonstrate three and four participants without
adding a user-visible theory. The dedicated QF_UFLRA route, its experimental
generic route, legacy callbacks and scheduling remain unchanged. New adapters
may use unchanged Primo kernels; no other solver implementation is a source.

The source baseline is `75d5177` (solver sources identical to `287b743`). The
retained Release/CaDiCaL 3.0.0 baseline binary has SHA256
`5a671d47882f555f3522c92b7b6c05a76c3bbdfde6ec2cb714b69e76c519b3f7`.
Its full-corpus results are recorded in `run-log.json`; they are historical
comparison evidence, not validation of the migration.

## Sources and proof obligations

Read for this migration:

- de Moura and Bjorner, *Model-based Theory Combination*, ENTCS 198(2),
  2008, pp. 37–49, DOI <https://doi.org/10.1016/j.entcs.2008.04.079>;
  local `papers/model-guided-no.pdf`, Sections 2.1, 4, 5.1 and 5.2.
- Bruttomesso, Cimatti, Franzen, Griggio and Sebastiani, *Delayed Theory
  Combination vs. Nelson-Oppen for Satisfiability Modulo Theories: A
  Comparative Analysis*, LPAR 2006, LNCS 4246, pp. 527–541,
  [author manuscript](https://es-static.fbk.eu/people/griggio/papers/lpar06_dtc.pdf),
  Sections 2 and 3, Figure 4. Complete arrangements do not require convexity
  or complete equality deduction. We do not claim the paper's NO-emulation
  branching bound or its specific last-UIP/negative-first strategy.

Model coincidences create reversible SAT decisions, never entailed equalities.
Only explained consequences become clauses. A component's complete projection
must satisfy every assigned interface relation in its scope. After mutation,
check that obligation again and recompute comparison edges, including edges
not mentioned in the mutation request. Bounded model guidance retains the
complete-arrangement fallback. This is a transition subset, not Appendix A's
full `DPLL(T^M)` implementation; this migration adds no new simplex algorithm.

The following many-sorted normalization is an original implementation design.
It applies the arrangement construction to each connected sharing group.
Stable infiniteness means **simultaneously** infinite domains for the declared
shared sorts, not separate models witnessing infiniteness one sort at a time.
Bool and other finite shared domains are not admitted by this protocol.
Disjoint interpreted signatures and explicit atom ownership are preconditions.

## Representation and operations

Implemented candidate:

- `theory::TheoryComponent`: solve-scoped component lifecycle and immutable
  descriptor; no SAT backend or SMT-LIB AST. It owns preparation, incremental
  state, explanations and model contributions. Terms are referenced by stable
  `TermId`, never arena references retained across construction.
- `smt::TheoryCombinationPlan`: validated descriptor copies, atom-owner routes,
  and normalized per-sort scopes. Unowned/ambiguous atoms and unsupported
  signature or sort combinations are Unsupported, not UNSAT.
- `smt::ComponentModelCoordinator` and `smt::ComponentDispatcher`: component
  collections and recipe priorities;
  no EUF/LIA name tests. Online checking remains EUF-first and model guidance
  arithmetic-first through those priorities. Assignment deltas use precomputed
  routes, not a component scan per literal.
- `smt::solve_component_recipe` owns Boolean abstraction, equality bridge
  clausification, assumptions and failed-core blocking, component preparation,
  typed stop handling and complete-arrangement restart. `CombinationRecipe`
  supplies component factories and independent online/model priority orders.
  Normal disagreement creates one equality choice per round, preserving the
  baseline policy; only explicit mutation candidate lists can batch choices.
- `theory::ComponentModelFragment` declares authoritative typed
  values, retained facts and symbol contributions. Local domain identities are
  namespaced by component. `validate_component_fragment` checks the exact
  accepted token, native authoritative coverage, uninterpreted shared-variable
  coverage, symbol ownership and agreement with the accepted partition or
  complete arrangement. `assemble_component_model` identifies local domain
  labels using shared terms before constructing evaluated function tuples.
  Identification may not collapse two distinct values from one component.
  Conflicting native values, facts or rows are errors, never last-write-wins.
  The resulting `core::Model` owns an immutable interpretation. Conversion
  steps replay in reverse execution order, and final assertions are evaluated;
  the session separately checks original assertions with `--check-model`.

Production `UfliaEufComponent` and `UfliaLiaComponent` currently live beside
their preparation helpers in `solver_engines.cpp`. They expose no SAT backend
or private model state through `TheoryComponent`. `ComponentAssignment` owns
the dense variable index, assignment trail and decision-level boundaries;
callbacks route deltas without rescanning the full atom set. Interface growth
detaches the dispatcher before extending preparation and rebuilding live
callbacks. LIA exports exact values from its private integer-completed model;
EUF exports retained facts, not arbitrary numeric anchors. Arithmetic remains
first for full-model reconciliation and EUF first for online checking.

Two transitional duplications preserve the explicitly frozen routes:
`ComponentLraExecution` ports Primo's existing online arithmetic execution with
phase publication replacing its backend reference; the original callback is
unchanged. `FragmentModelBuilder` extends Primo's existing typed reconstruction
algorithm behind the fragment boundary; the old typed builder remains unchanged
for QF_UFLRA and the other routes. These are not new simplex or reconstruction
algorithms, nor copies from another solver. Moving engine-local preparation
helpers into theory-owned files can follow without changing the public protocol.
The driver accepts purified atoms; admission rejects escaped mixed arithmetic
applications rather than assigning an ambiguous owner. The QF_UFLIA front-end
purification transformation itself retains the existing semantics.

The new coordinator validates projection epochs, component identity and exact
scope coverage before comparing partitions. A descriptor change after planning
is rejected. Care capability gates care queries; care pairs rank decisions, not
acceptance. The dispatcher retains per-component assignment buffers and an
indexed SAT-variable route. Partial checks visit only dirty components in recipe
order; reasons for surviving root propagations survive unrelated backtracking.
Typed cancellation/resource/unsupported results are saved for the owning solve
loop, never converted to an empty clause.

`tests/unit/component_combination_tests.cpp` checks 432 three/four-component
cycle/order/policy combinations using CaDiCaL against exhaustive partition
enumeration. Additional tests cover two-sort nonparticipants, disconnected
scopes, capability rejection, nonconvex disjunctions, malformed/stale models,
mutation side effects, fixed decisions and incremental explanation lifetime.
Another 432 cycle/policy/online combinations use `solve_component_recipe`
end-to-end, including SAT assumptions, sparse growth, callbacks and fragment
model assembly. `model_tests.cpp` deliberately supplies stale exports, missing
shared bindings, partition-inconsistent values, conflicting numeric bindings,
fractional Int values and duplicate function rows. It verifies component-local
domain remapping and immutable table ownership. These tests are correctness
evidence, not performance or corpus-validation evidence.

Foundation verification (2026-09-08): `make check` passed (29 CTest targets and
107 Python tests); `make asan-test` passed all 29 targets; `make docs` and
`make lint` passed. Focused clang-tidy on the four new `.cpp` files passed
without warnings. The whole-tree tidy attempt reached the local 120-second
limit; its completion belongs to the SLURM verification gate. The advisory
include scan was rerun on the final new files after a concurrent-edit snapshot
proved unusable. No production fuzz/performance claim follows from these tests.

Connected-candidate verification: all 29 CTest targets and 111 Python tests
pass; lint and documentation builds pass. A byte comparison against `75d5177`
confirms the QF_UFLRA engine tail and legacy callback region are unchanged
(the removed helper belonged only to QF_UFLIA). The first ASan pass caught a stack-use-after-return in the new EUF
adapter: `OnlineEufPropagator` borrowed options created inside `reset()`.
Options now belong to the adapter, precede the callback in destruction order,
and the callback is destroyed before configuration/preparation changes. The
existing evaluated-argument regression reproduced this under ASan; after the
fix `make asan-test` passes all 29 targets. Focused static review of the new
driver, assembler, dispatcher, coordinator and their tests reports no owned
code warning. Subsequent whole-tree review and generated-campaign results are
recorded below and in the run log; performance remains pending. A passing
ordinary build was not used as lifetime evidence.

For each sort, build the component graph whose edges mean occurrence of the
same interface variable. In each connected group, form the union of variables
occurring in at least two original components. Register that whole union with
each group participant, including unconstrained vocabulary extensions.
Projections must cover that scope exactly. A component outside the group is
never compared or mutated for it. Pair keys contain sort and canonical term IDs.

Example: A has `{x,y}`, B `{y,z}`, C `{x,z}`. All three receive `{x,y,z}`;
otherwise independent pairwise agreements could miss `x=y`, `y=z`, `x!=z`.
Disconnected groups and different sorts keep distinct scopes. EUF+LIA keeps
exactly its old shared-Int intersection, avoiding an interface-size regression.
Plan construction uses indexed occurrence lists and connected components;
projection storage is linear in normalized scope size. Complete fallback may
enumerate quadratically many same-scope pairs, as in the baseline. Model-guided
comparison should use class spanning edges rather than an unconditional pair
cross-product. No live row insertion into a trailed tableau is introduced:
interface growth uses the established callback rebuild boundary.

## Acceptance checklist

### Adding another component

This is the implemented extension path, not a claim that arbitrary SMT theories
already satisfy the combination preconditions. A new kernel still needs its
own paper-grounded design, soundness argument and fragment tests.

1. Implement `theory::TheoryComponent` with a solve-owned lifetime. Keep kernel
   state private. Publish an immutable `ComponentDescriptor` with a unique ID,
   supported fragment, disjoint interpreted families and owned function symbols.
   Declare convexity and joint stable infiniteness accurately; complete equality
   propagation is a separate capability, not a consequence of writing "convex".
2. Define exact `owns_atom` and `interface_variables` behavior on purified terms.
   Every original or generated atom must have exactly one owner. Shared variables
   must be free constants. Support registration of unconstrained scope extensions
   when the sharing graph requires them; do not infer the scope from only the
   component's original atoms. Finite shared domains and overlapping interpreted
   signatures remain outside this protocol.
3. Implement registration, reset, level changes and assignment deltas. With an
   online recipe, declare and implement incremental support. Explanations must
   remain valid after unrelated backtracking; resources and unsupported cases
   return typed stops. `encode_equality` returns an equivalent Boolean formula;
   the SMT driver, not the kernel, creates SAT clauses and owns the backend.
4. Provide complete, fresh model projections when claiming that capability.
   Missing projection support selects complete arrangements. Mutations must
   preserve assigned relations and advance model revision. To produce a final
   model, declare export support and return an owned fragment for the exact
   accepted token. Cover shared bindings, namespace domain identities locally,
   and export only owned symbol interpretations. A shared sort may have at most
   one authoritative value owner in each connected group.
5. Add a factory to `smt::CombinationRecipe`. Its Boolean argument selects
   model-guided versus complete mode, including fallback reconstruction. Factory
   order sets reconciliation priority; `online_order` must be a permutation of
   every component index when online execution is enabled. Set the bounded
   decision budget deliberately. No component-count or theory-name branch is
   needed in the collection driver, and no QF_UFLRA route change is required.
6. Extend the independent partition-oracle tests in
   `tests/unit/component_combination_tests.cpp`, including reordered three/four
   participant recipes, disconnected sorts, sparse growth and complete fallback.
   Exercise `solve_component_recipe` with model production, not just the
   coordinator. Add negative fragment/lifetime/explanation tests, exact-profile
   fuzzing, independent model replay and paired performance preservation before
   admitting the new production recipe.

### Migration gates

- [x] Validated contracts, ownership and capability planning.
- [x] Participation normalization and three/four-component SAT integration tests.
- [x] Freshness, mutation, fixed-decision and typed failure regression tests.
- [x] Collection dispatcher and production incremental EUF/LIA adapters.
- [x] Generic model export/composition, including corrupted-model rejection.
- [x] Full production QF_UFLIA migration; frozen QF_UFLRA source preserved.
- [x] Validator resource classification and explicit Dolmen memory budgets.
- [x] Check, ASan, lint, docs, whole-tree static/code and efficiency review.
- [x] Exact-candidate generated and real-seed mutation coverage for all selected
  profiles; at least 3,000 default generated cases. Resource-only outcomes are
  triaged below, not counted as passes or an unconditional green campaign.
- [ ] Independent Dolmen replay of every candidate real-corpus SAT row, separate
  from speed measurement; resource failures are unvalidated, not invalid models.
- [ ] Paired baseline/candidate SLURM sample, watchlist, full 659 QF_UFLIA and
  five-logic preservation suite, 60-second full-corpus cap. Alternate run order
  on the same node, then repeat with reversed order. Each logic must stay within
  1% for PAR2 and common-solved runtime; any lost solve gets three paired reruns
  and a loss in at least two blocks acceptance. Report families and all outcomes.
- [ ] Reuse recorded reference answers/timings; rerun only missing evidence or
  genuine disagreement. Preserve binary/corpus/build/option provenance.
- [ ] Coherent verified commits and canonical run-log entries for every gate.

No new permanent CLI profile is planned. Default promotion requires all gates;
the frozen external baseline is the A/B control. Operational details belong in
the existing testing, benchmarking and model-validation skills, not AGENTS.md.

`slurm_component_migration_compare.sbatch` takes explicit baseline/candidate
binaries and expected revisions, runs both on the same allocated node, reverses
their order for a second paired round, and records hashed provenance through
`benchmark_model_replay.py`. An optional ordered manifest selects the sample or
watchlist before sharding. It launches no reference solver. Each candidate
round's SAT rows are subsequently replayed by a separate validation job.
Both scripts explicitly require one node and one task: they launch one serial
Python harness, not a distributed program. Select eligible timing nodes through
exclusions or constraints; without the explicit node-count limit, a multi-node
`--nodelist` can reserve every listed node. Inspect the allocated node/task
counts immediately after submission. The cluster's allocation-only check with
`--nodes=1 --ntasks=1` and a two-node list selects just one node; it does not
reject that combination of arguments.

`slurm_component_loss_rerun.sbatch` performs exactly three baseline/candidate
pairs on an explicit lost-solve manifest, alternating order within one
allocation. Use one input per shard with its default ten-minute allocation and
60-second solver budget. It forwards the same profile options and records the
same hashed provenance as the full comparison; no reference solver is launched.
A candidate loss in at least two of the three pairs blocks acceptance. These
focused repetitions do not replace the full-corpus aggregate timing thresholds.
Any SAT answers produced by the reruns still require separate model validation.
`summarize_component_pairs.py --loss-reruns --expected-count N ROOT` validates
all three pairs' provenance and reports per-input loss counts and the two-loss
threshold separately from the original timing verdict. Process errors, wrong
answers or unsupported output cannot be accepted as ordinary cutoff variation.

`slurm_component_migration_fuzz.sbatch` covers the full 16-profile cross-product
of preprocessing auto/off/basic/strong, model guidance on/off and online mode
on/off. Four shards give the default profile 3,000 generated mixed inputs and
each other profile 1,000 (18,000 total). One shard per profile first checks all
tracked regression and QF_UFLIA smoke seeds against the reference, including
exit-status and declared-answer checks. SAT outputs require direct Dolmen
validation. `slurm_component_migration_mutation.sbatch` uses the same profiles
and deterministic smallest-per-family real seeds; oversized seeds are errors,
not silently omitted families. Its retained mutants undergo separate model
validation with the exact same solver options.
The standalone TypeFuzz gate explicitly selects the existing linear-integer
configuration, but operator selection alone is insufficient: generative TypeFuzz
can replace a literal coefficient inside an existing multiplication with a
nonconstant expression. Both attempts are retained as rejected harness evidence,
not valid-input campaign coverage. The qualification matrix therefore uses the
existing seeded OpFuzz profile (`model_linear_ops.txt`), which changes operators
without generating new operands. The existing typed-model mutation gate checks
original seeds and every retained mutant with status-aware reference comparison
and independent model validation. Default mutation iterations are 100 per family
seed, bounded by the campaign wall time. No candidate rejection is ignored to
make a gate pass; out-of-fragment attempts remain in the run log.
The original-seed precheck has a separate 60-second budget; short mutated-input
checks retain a five-second budget. In this qualification matrix,
`classify_mutation_resources.py` permits collection to continue after explicitly
classified timeout/unknown/resource rows. It rejects crashes, disagreements,
bad inputs, invalid models, changed answers and missing evidence. The original
nonzero stage status still fails the final gate; continued collection is not a
pass. All seeds remain included. Ordinary typed-model gate callers retain
fail-fast behavior unless they explicitly request resource-only collection.
Mutant answer comparison ignores inherited seed status metadata, which need not
survive an operator mutation, but still compares the two solver answers and
process statuses. Original-seed status checks remain enabled.

Whole-source clang-tidy and the advisory include scan completed on SLURM (105
non-generated C++ files each). The include review removed three unused headers
from the migrated engine and replaced the indirect exception-type dependency
with its owning plan header. The unchanged `Goal` shared-pointer constructor has
one analyzer leak warning: manual ownership review and the ASan leak checks do
not support the warning; no suppression or unrelated source change was added.

`summarize_component_pairs.py` checks completed shard provenance, matching
input hashes/options/timeouts, stable binary identities, both paired rounds and
node records before reporting timing ratios. It reports family summaries and
lost solves separately. Its timing result does not certify models or replace
the recorded-reference answer join.
`join_recorded_reference_answers.py` performs that join without running any
solver. It requires complete recorded shards and matching input hashes, checks
all decisive recorded answers for disagreement, and reports absent reference
answers separately. Incomplete historical shards are not reused as complete
evidence. Candidate unsolved rows remain unsolved, not verified answers.

The paired sample and watchlist pass their aggregate timing gates, but wisas
is slower in both. Its SAT decisions/propagations and combination rounds match
the baseline; reconciliation overhead is a profiling target. The larger growth
timer alone is not comparable: the new timer also encloses callback rebuilding,
which was outside the old growth timer. `slurm_component_profile.sbatch` builds
a separate `-pg` binary and collects repeated gprof samples without changing
either retained timing binary. Instrumented times are diagnostic, not headline
performance evidence. No new algorithm is inferred from the timers alone.

## Qualification evidence through the full QF_UFLIA stage

The retained `2413397` candidate passed 17,996 of 18,000 generated answer checks;
three node-limit unknowns and one timeout remain inconclusive. The default
profile passed all 3,000 cases. All 9,134 generated SAT models passed Dolmen,
and all 96 tracked regression/profile checks passed. Real-seed operator
mutation covers all four families under all 16 option profiles: 2,817 retained
mutants yielded 2,453 decisive answer agreements and 364 resource outcomes.
Separate model checks yielded 1,514 direct Dolmen passes, 941 UNSAT rows and
362 timeouts. Answer and replay counts differ because two initially incomplete
answers became decisive on replay. None of these resource rows is a pass;
the collection harness retains a nonzero final status. There were no wrong
answers, invalid models, crashes, parser/type errors or reference disagreements.

The full 659-input QF_UFLIA comparison used two same-node, reversed-order
baseline/candidate rounds with a 60-second cap. The baseline solved 571 inputs
in each round; the candidate solved 574 and 575. There were no lost solves,
errors, unsupported results or answer disagreements. Combined PAR2 decreased
from 23,309.092340 to 22,808.494721 seconds (2.15%); common-solved runtime
decreased from 2,189.092340 to 2,139.664572 seconds (2.26%). All 810 candidate
SAT replay rows passed Dolmen directly with an 8G checker budget; all 64 replay
shards have complete coverage and unchanged binary/input hashes. Every decisive
candidate answer agrees with a hash-matched recorded reference answer. No
reference solver was rerun for these comparisons.

This is not a uniform family speedup. Certora gained two solves in both rounds,
but its common-solved runtime increased 22.59% (72.301020 to 88.635538 seconds
across both rounds). TwoSquares and mathsat common runtime decreased 0.66% and
2.11%; wisas decreased 8.16% over the full family despite the small-probe
regression. The extra fourth gain in the second round is near-cutoff variation,
not evidence of four robust additional solves. Candidate-only gprof identified
routing and pair ordering as future optimization targets; no speculative
optimization was added after the aggregate gate passed.

The remaining preservation suite runs QF_UF (7,503), QF_LIA (13,306), QF_LRA
(1,753) and QF_UFLRA (1,284) with the same frozen binaries and two paired rounds.
The completed 659-input run is reused as the QF_UFLIA leg. Model replay runs on
a separate node only after its corresponding timing shard completes. Recorded
reference results are reused by content hash. Full acceptance still requires
each logic's 1% PAR2/common-runtime limits, any required three-pair lost-solve
reruns, and validation of every candidate SAT row. Exact submissions, artifacts
and resource classifications are in `run-log.json`.

QF_UFLRA preservation is now complete for timing and models: both binaries solve
the same 1,270/1,284 inputs in each round (759 SAT, 511 UNSAT, 14 timeouts),
with no lost or gained solves. Combined PAR2 increases 0.22% and common-solved
runtime 0.73%, within the 1% limits. All 1,518 candidate SAT replay rows pass
Dolmen; all 128 coverage files are complete with unchanged hashes. Family
common-runtime changes are FFT +0.46%, cpachecker-bmc -0.03%,
cpachecker-induction +0.78% and mathsat +0.28%.

Each QF_UFLRA round has 1,265 decisive recorded-reference agreements and five
cases where both references previously timed out at 60 seconds. Those five
match the frozen Primo baseline and the input's declared status; two SAT
answers additionally have validated models. The three UNSAT answers remain
without decisive reference corroboration. The join reports these as
inconclusive reference evidence, not disagreements or missing artifacts;
no redundant reference sweep is launched to replace known timeout records.

QF_UF preservation also passes after the prescribed lost-solve follow-up. The
candidate solves 7,495/7,503 inputs in each round; the baseline solves 7,494 and
7,496. Combined PAR2 increases 0.42% and common-solved runtime 0.48%, within the
1% limits. All 6,284 candidate SAT replays pass Dolmen, with 256 complete
coverage records and unchanged hashes. PEQ and SEQ common runtimes increase
about 4.5%; this is not a uniform family speedup. One gained QG solve in round
zero and one lost PEQ solve in round one remain recorded, not erased as noise.
The lost PEQ case solves UNSAT on both binaries in all three focused pairs,
matching recorded Z3, so the two-of-three repeated-loss threshold is not met.
Its focused runtime ratio is 1.0109; the preservation limit applies to the
full-corpus aggregate, while the follow-up tests repeated loss of a solve.
Ten other UNSAT inputs have only known reference timeouts, agree with the
frozen Primo baseline and declared status, and remain explicitly without
decisive reference corroboration. Exact files and both rounds' family results
are retained in the run log and artifacts.
