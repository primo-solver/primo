# N-component combination migration

Status: implementation in progress. The production QF_UFLIA route is connected
to the collection driver, dispatcher and fragment assembler in the working
candidate. It is **not yet qualified** by fresh fuzzing or SLURM performance
gates. The frozen external binary remains the baseline. This page is the
implementation checklist, not a completion or performance claim.

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
code warning. Whole-tree static review, fresh fuzzing and performance remain
pending; a passing ordinary build was not used as lifetime evidence.

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

- [ ] Validated contracts, ownership and capability planning.
- [ ] Participation normalization and three/four-component SAT integration tests.
- [ ] Freshness, mutation, fixed-decision and typed failure regression tests.
- [ ] Collection dispatcher and production incremental EUF/LIA adapters.
- [ ] Generic model export/composition, including corrupted-model rejection.
- [ ] Full production QF_UFLIA migration; frozen QF_UFLRA source preserved.
- [ ] Validator resource classification and explicit Dolmen memory budgets.
- [ ] Check, ASan, lint, docs, whole-tree static/code and efficiency review.
- [ ] Exact-candidate generated and real-seed mutation gates for all selected
  profiles; at least 3,000 default generated cases.
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
