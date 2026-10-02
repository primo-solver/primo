# Nelson-Oppen theory combination

Scope: the retained dedicated QF_UFLRA route, reviewed 2026-09-09.
The N-component QF_UFLIA route is documented separately in
[](n-way-combination-migration.md); [](current-status.md) maps both routes and
their revision-scoped qualification.

Primo implements a specialized convex Nelson-Oppen combination for EUF and
rational linear arithmetic in `QF_UFLRA`. It includes purification, a sparse
shared-equality registry, explained equality exchange, a default model-guided
compatibility check, lazy SAT-visible equality decisions, exact LRA fallback,
and typed mixed model reconstruction. This dedicated engine does **not** use
the new component collection driver. Neither engine claims the complete
`DPLL(T^M)` transition system.

The classical basis is Nelson and Oppen's cooperation by equality sharing. The
model-guided parts use de Moura and Bjorner, *Model-based Theory Combination*,
Sections 4, 5.1, and 5.2, DOI
<https://doi.org/10.1016/j.entcs.2008.04.079>. The exact arithmetic checks and
their explanations use the existing incremental LRA tableau described in
[](arithmetic.md). Detailed readings and the implementation checklist are in
[](papers/nelson-oppen-closure-methods.md) and
[](papers/model-based-theory-combination.md).

## Supported combination

This QF_UFLRA path has two component theories:

- EUF owns uninterpreted applications, predicates, congruence, and their
  explanations;
- LRA owns linear comparisons over exact rational values and its tableau;
- the SMT layer owns purification, SAT abstraction, the shared Real interface,
  equality exchange, and coordination of candidate component models.

After purification, the component signatures are disjoint except for equality
and shared constants of sort `Real`. Both components are stably infinite on
this interface, and EUF and rational LRA are convex: when their conjunction
entails a disjunction of interface equalities, one of those equalities is
entailed. These are the Nelson-Oppen preconditions that let explained equality
sharing replace enumeration of arbitrary equivalence relations in the
implemented fragment.

Only a Real constant that occurs in both an EUF atom and a normalized LRA atom
is an interface term. A zero coefficient after LRA normalization does not make
a term shared. Real constants used only by EUF or only by LRA stay local to that
component. Boolean terms are coordinated through SAT and the Boolean/EUF bridge,
not through a Nelson-Oppen arrangement; see [](euf-boolean-arguments.md).

The mixed solver is selected after preprocessing when a formula has arithmetic
and either an equality or an EUF predicate. With the default
`--mixed-dispatch=narrow`, a propositional Boolean constant alone does not route
an otherwise pure LRA formula through the mixed solver. Pure UF, LRA and LIA
retain their direct theory paths.

## Purification

`purify_mixed_arithmetic_applications()` runs before theory dispatch and rewrites
the supported mixed terms into theory-owned atoms. Rewriting is memoized by
`TermId`, so a repeated source term uses the same proxy.

For a Real-valued uninterpreted application in arithmetic, for example

```lisp
(<= (+ (f x) 1) y)
```

Primo creates a fresh Real constant `p`, gives EUF the definition `p = f(x)`,
and gives LRA `p + 1 <= y`. For a non-constant arithmetic term used as a Real
argument of an uninterpreted symbol, for example `P(x + 1)`, it creates a fresh
Real constant `q`, rewrites the EUF atom to `P(q)`, and gives LRA the two bounds
`q <= x + 1` and `x + 1 <= q`.

Nested applications and repeated mixed subterms are handled recursively. A
Real equality produced by the ordinary assertion pipeline is lowered to two
non-strict arithmetic comparisons before LRA dispatch. The EUF proxy definition
remains an equality because it belongs to congruence closure. The postcondition
checked by the mixed dispatcher is therefore:

- no LRA atom contains an uninterpreted application;
- no supported EUF atom contains an unpurified non-constant Real subterm;
- communication between the two components is only through shared Real
  constants and their equalities.

Purification definitions are permanent assertions for the current
`check-sat`; they are not temporary model guesses. Counters under
`:no.purification.*` report definitions, reused proxies, memoized rewrites, and
the no-site fast path.

## Ownership and lifetime

| Owner | State | Lifetime and responsibility |
| --- | --- | --- |
| `core::SymbolTable` / `core::TermManager` | The table owns sorts/symbols; the manager owns terms, including fresh purification/equality terms | Solver context; the table outlives the manager. Stable `TermId`s, not borrowed nodes, cross allocating calls. |
| `BooleanClausifier` | SAT literals for Boolean structure and theory atoms | One mixed solve; maps formula atoms initially and lazily materializes a missing interface equality when the coordinator requests one. |
| `TheoryCombinationManager` | Interface terms, canonical equality entries, a shared assignment view, arrangement values, classifications, shared-model projection, and lazy equality-decision state | One mixed solve. Arrangement values have their own undo log; SAT truth is read from the shared view. Shared-model data is round-scoped. |
| `PreparedFlatTheory` and `IncrementalFlatAssignmentState` | Flattened EUF definitions and congruence closure | EUF-owned structural preparation and assignment/model scopes. The manager stores no congruence closure. |
| `PreparedLraTheory`, `MixedLraModelEpoch`, and `LraTableau` | LRA atom mapping, retained prepared/check state, exact rational assignment, and explanations | LRA-owned component state. The manager stores only its projection on shared terms. |
| `solve_qf_uflra_engine_impl()` | The mixed fixed-point and model-reconciliation control flow | One `check-sat`; it invokes the components and decides when SAT must be re-entered. |
| `sat::IpasirUpBackend` | Clauses, assumptions, observed variables, assignments, and failed assumptions | SAT-owned. It never contains theory terms. |

The manager's `SharedModelState` is valid only for its current
shared-model round and generation. It stores LRA value classes, their term-to-
class map, deterministic spanning equalities, explained equalities, equalities
absorbed into the candidate EUF model, and selected LRA separation atoms. A new
round or relevant rollback invalidates it. It deliberately stores neither an
EUF closure nor an LRA tableau.

`SharedEqualityDecisionState` is solve-scoped. It owns pending, active, and
already-tried lazy equality literals. The manager owns this lifecycle, while
the mixed coordinator performs the IPASIR-UP assumption solves and learns
failed-assumption blocking clauses.

## Shared-equality registry

`TheoryCombinationManager::rebuild_from_interface_terms()` establishes the
typed Real interface. An equality entry is keyed by the canonical unordered
pair `(min(lhs,rhs), max(lhs,rhs))` and records:

- the equality term and SAT literal, if materialized;
- whether the pair is still missing a visible atom;
- its combination value: unknown, equal, or disequal;
- round-local classifications such as EUF-implied, LRA-implied, SAT-forced,
  assigned-false, EUF-mergeable, EUF-forbidden, or model-internal.

The current SAT assignment of a registered equality is queried through the
bound `sat::TrailView`, returning unknown, true, or false. It is not stored
as another assignment snapshot in the registry.

The registry is sparse. `ensure_interface_pair()` validates same-sort interface
ownership but does not itself allocate a SAT variable. A producer that actually
needs a missing pair asks the clausifier for `(= s t)`, registers the resulting
literal, and, during online propagation, asks IPASIR-UP to observe the fresh
variable. The diagnostic count of all possible shared Real pairs is not an
allocation request.

`push_level()` and `pop_to_level()` restore the combination arrangement values.
SAT assignment rollback belongs to the shared assignment owner described in
[](dpllt.md); {source}`the manager's assignment query and rollback
<src/smt/theory_combination.cpp#L625-L699>` use that boundary. Per-round
classifications are cleared or rebuilt when the coordinator starts another
candidate-model round.

## Entailed equality exchange

An equality may be propagated only with an explanation proving it from the
current theory assignment. Accidental equality in one candidate model is not an
entailed equality.

EUF projects each shared congruence class as a deterministic spanning set. Its
reasons come from congruence-closure explanations. LRA has two producer layers:

- the Section 5.1 producers `FixedEq`, `EqRow`, `EqOffsetRows`, and `EqRows`
  derive cheap equalities and their bound/row explanations;
- the exact fallback considers unresolved pairs that are equal in the checked
  LRA model and proves `s = t` only when both strict alternatives `s < t` and
  `t < s` conflict with the current LRA assignment.

The feasible LRA model is only a necessary-condition filter for the exact
fallback: terms with different values cannot be provably equal, while terms
with the same value still require both conflict checks. Exact probes reuse the
prepared mixed LRA state when its sparsity guards permit it and fall back to a
separate augmented tableau otherwise.

For reasons `r1, ..., rn`, forcing equality literal `e` adds the guarded clause

```text
not r1 or ... or not rn or e
```

and re-enters SAT. A pre-existing true equality needs no clause. A pre-existing
false equality turns the explanation into a conflict. A newly selected pair is
materialized lazily. Re-entering SAT makes both theory components observe the
same equality assignment and drives the convex exchange to a fixed point.

## Default model reconciliation

`qf_uflra_model_reconciliation` is enabled by default. It specializes de Moura
and Bjorner's model-based discipline to the convex shared-Real interface; it is
not a generic `DPLL(T^M)` state machine.

For each feasible mixed round, the coordinator performs these operations:

1. Check the EUF and LRA assignments. A component conflict is learned as a SAT
   clause and restarts search.
2. Build one scratch LRA assignment and greedily diversify the values of shared
   terms using freedom intervals. Equality literals assigned false by SAT are
   mandatory disequalities: mutation minimizes these violations before
   maximizing the number of LRA value classes.
3. If a mandatory disequality remains violated, search its two strict
   orientations. Failure of mutation is never treated as proof of equality;
   unresolved or inconsistent cases use the explained exact fallback.
4. Publish the resulting LRA value classes and a deterministic spanning set to
   `SharedModelState`.
5. Checkpoint EUF and tentatively merge each spanning edge. A merge is rejected
   if it directly violates an EUF disequality or if congruence joins terms that
   belong to different LRA value classes. Successful tentative merges are
   candidate-model choices, not lemmas.
6. Roll back the temporary compatibility scope. Materialize the canonical
   frontier of rejected edges lazily and try the frontier as one positive SAT
   assumption batch.
7. If the assumptions fail, learn a blocking clause from IPASIR-UP's failed
   assumptions, retain the surviving choices, and continue search. If all
   spanning edges are compatible, the component models agree on the shared
   equality relation.

Explained LRA equalities may be absorbed directly into the candidate EUF model
only together with their reasons and only when the LRA model agrees. An EUF
contradiction returns the composed theory conflict. An unexplained model-equal
pair is never inserted into this explained worklist.

The mixed solver retains one `MixedLraModelEpoch` keyed first by the true
interface equalities used to prepare LRA and then by the complete current LRA
assignment. Matching keys reuse the prepared theory or checked tableau;
different keys invalidate the corresponding cache. This is specialized
component-state reuse, not persistent generic model state.

An unmapped shared term, failure to satisfy a mandatory separation, or another
candidate-model invariant failure activates the conservative exact equality
path. The explicit `--nelson-oppen-arrangement-probe-limit` can instead stop
temporary LRA probing and return `unknown`.

## Cost boundaries

Purification visits the reachable term DAG with memo tables, so repeated source
subterms do not cause repeated rewrites. Interface discovery scans the EUF atoms
and the nonzero terms of normalized LRA polynomials. The manager's canonical
pair index uses expected constant-time hash lookup and stores only pairs that a
producer or visible atom requests.

For a shared class of size $k$, EUF and the LRA model projection publish at most
$k-1$ deterministic spanning edges. Section 5.1 LRA producers use indexed
fixed/row signatures rather than probing every shared pair. Model
compatibility, however, may project the shared EUF classes after tentative
edges, and the conservative exact fallback still has a worst-case quadratic
number of same-model-value pairs with up to two strict LRA checks per retained
pair. The sparse registry avoids quadratic SAT-variable allocation; it does not
remove this worst-case semantic search.

## Final validation and models

Before returning `sat`, the mixed path validates that the EUF equality relation
and the concrete LRA values can form one arrangement on shared Real terms. EUF
equalities and explained/internal LRA equalities must be preserved. Pairs that
EUF requires to remain distinct receive strict LRA separation atoms when
needed. The default reconciliation path can avoid the broad exact pair scan
after successful compatibility; its exact fallback remains the oracle when an
invariant is not established.

With `:produce-models true`, Primo composes:

- the ground EUF function/predicate table;
- values of user-visible nullary Real constants from the validated LRA model;
- purification definitions and preprocessing reconstruction assignments;
- model-internal shared equalities and selected LRA separation atoms.

An EUF-only Real equivalence class receives a deterministic fresh rational
representative. A class containing a genuine shared term is anchored to its LRA
value. These compatible component inputs feed `build_typed_model` in
`typed_model_builder.cpp`. Its private builder freezes the same owning
`core::Model` used by all other routes, with function tables keyed by evaluated
typed argument tuples. Goal-owned conversions replay backwards, and original
assertions are checked in debug builds or with `--check-model`. The session's
printer only serializes this interpretation; it cannot choose another row.
The dedicated engine does not yet export through QF_UFLIA's new
`ComponentModelFragment` protocol. See [](model-interpretation.md).

## Propagation modes

`--nelson-oppen-prop` controls LRA equality production, not whether mandatory
combination checks run:

| Mode | Behavior |
| --- | --- |
| `exact` | Keeps mandatory convex closure and exact fallback but disables opportunistic LRA equality producers. `off` is a compatibility alias for this mode. |
| `final` | Default. Runs the Section 5.1 producers and model reconciliation at mixed-theory quiescence, with exact fallback. |
| `online` | Adds callback-time paired-bound propagation and still retains final reconciliation and exact fallback. |

In online mode, `OnlineLraPropagator` publishes `s = t` only after observing
both true bounds `s <= t` and `t <= s`. The reason contains their two blocking
literals. `MixedTheoryPropagationDispatcher`, CaDiCaL's one external
propagator, maps the candidate to the shared registry. If no equality atom
exists, it creates the term and a fresh observed SAT variable from
`cb_propagate()` before returning the propagated literal. No all-pairs equality
preallocation is required.

`--qf-uflra-online-euf` is a separate default-off experiment that reuses live
incremental EUF callback state. It is not implied by `--nelson-oppen-prop=online`.
The default-off `--model-equality-branching` heuristic is also separate from the
default reconciliation frontier. It explores additional LRA-model-equal pairs
under configurable budgets and policies in the explained exact profile selected
with `--no-qf-uflra-model-reconciliation`.

The propagation mode and reconciliation option are orthogonal axes. In
particular, `--nelson-oppen-prop=exact` does not by itself disable default model
reconciliation; it disables the opportunistic LRA producer layer.

## User options

| Option | Default | Effect |
| --- | --- | --- |
| `--mixed-dispatch={narrow,wide}` | `narrow` | Controls which preprocessed formulas enter the mixed solver. |
| `--nelson-oppen-prop={exact,final,online}` | `final` | Selects the equality-production schedule described above. |
| `--no-qf-uflra-model-reconciliation` | reconciliation on | Uses the explained exact equality-sharing path instead of default shared-model reconciliation. |
| `--no-qf-uflra-fixed-elimination` | mixed fixed elimination on | Disables the mixed LRA elimination that exposes `EqRow` consequences. Pure LRA has a separate default-off option. |
| `--model-equality-branching` | off | Enables the older experimental model-equality assumption heuristic when default reconciliation is disabled. |
| `--no-model-equality-mutation` | mutation on when reconciliation or branching is active | Disables scratch freedom-interval diversification. |
| `--model-equality-mutation-scope={all,care}` | `all` | Restricts mutation variables to all shared terms or EUF care-pair endpoints. |
| `--model-equality-branch-policy={first,ranked,adaptive,care-queued}` | `first` | Selects the experimental branch heuristic's queue policy. |
| `--model-equality-branch-budget=N` | `16` | Caps assumptions made by the experimental branch heuristic. It does not cap default reconciliation. |
| `--qf-uflra-candidate-final-scan` | off | Enables an experimental candidate-first exact final scan. |
| `--qf-uflra-online-euf` | off | Enables experimental live EUF callback reuse. |
| `--experimental-generic-model-combination` | off | Routes checked projections through the older `ModelBasedCombinationCoordinator`; does not select the N-component driver or replace the dedicated engine. |
| `--nelson-oppen-arrangement-probe-limit=N` | unset | Returns `unknown` after `N` temporary LRA consistency probes. |
| `--nelson-oppen-diagnostics` | off | Adds the read-only interface and purification diagnostic scan. |

## Statistics

The implementation publishes detailed counters, initialized even when a
mechanism does no work. The most useful groups are:

- dispatch and interface: `:solver.qf-uflra`, `:no.mixed.*`,
  `:no.interface.shared-real-terms`, `:no.interface.materialized-*`, and
  `:no.interface.sat-equality-atoms`;
- purification: `:no.purification.*`;
- explained closure: `:no.interface.convex-rounds`,
  `:no.interface.euf-to-lra-*`, `:no.interface.lra-to-euf-*`,
  `:no.interface.lra-opportunistic-*`, and
  `:no.interface.lra-exact-fallback-*`;
- model filtering and mutation: `:no.interface.lra-model-filter-*` and
  `:no.interface.lra-model-mutation-*`;
- default reconciliation: `:no.shared-model.*`, especially
  `compatibility-*`, `forbidden-*`, `targeted-separation-*`,
  `split-frontier-*`, `converged-rounds`, `exact-fallbacks`, and
  `validation-failures`;
- lazy assumptions: `:no.interface.model-equality-pending-batches`,
  `assumption-solve-calls`, `failed-assumptions`, and `blocking-clauses`;
- online propagation: `:no.online.*`, including lazy equality creation,
  observed variables, propagations, and reason literals;
- resource stops: `:no.interface.lra-extra-atom-probe-limit-hit` and the
  arrangement/targeted-separation cap counters.

`:no.online.shared-real-pairs` is the full diagnostic pair count. It must not be
interpreted as the number of allocated equality atoms. In the lazy online path,
`:no.online.precreated-equality-atoms` remains zero.

## Soundness invariants

The mixed path relies on these boundaries:

1. Every atom is theory-pure after purification.
2. Only constants present in both component atom sets enter the shared Real
   interface.
3. Every propagated equality has a theory explanation valid under the current
   SAT assignment.
4. Equality in a candidate model is a choice or filter, never an entailment.
5. Every SAT-visible interface equality has one canonical registry entry and is
   created lazily.
6. A candidate EUF merge is accepted only if its congruence consequences agree
   with the LRA value partition.
7. Backtracking restores arrangement values and assignments and invalidates
   round-scoped model projections when required.
8. Pure-theory solving does not instantiate the mixed dispatcher or shared
   model state.

Focused assertions for these invariants are in
`tests/unit/theory_combination_tests.cpp` and the mixed section of
`tests/unit/qf_lra_solver_tests.cpp`. The tests cover purification and proxy
reuse, bidirectional equality exchange, exact reasons, no propagation of
accidental model equality, model mutation, congruence consequences, lazy split
batches, failed-assumption blocking, online lazy atom creation, model
composition, and pure-theory isolation. Python coverage includes
`tests/python/test_qf_uflra_smoke_fuzz.py`, finite-domain differential fuzzing,
and model validation.

## Implementation map

- `src/smt/theory_combination.hpp` and `.cpp` define the interface summary,
  equality registry, trailed arrangement state, shared-model projection, and
  lazy equality-decision lifecycle.
- `src/smt/preprocessing.cpp` owns mixed purification; `solver_strategy.cpp`
  owns post-preprocessing dispatch.
- `src/smt/solver_engines.cpp` owns `solve_qf_uflra_engine_impl()`, component
  preparation, reconciliation, exact fallback, online dispatcher plumbing and
  final arrangement validation. `typed_model_builder.cpp` owns the retained
  typed model adapter; `solver_session.cpp` checks and serializes its result.
  `check_sat.cpp` is a thin public wrapper, not the algorithm implementation.
- `src/theory/equality/` owns flattened congruence closure and EUF
  explanations. [](euf.md) is its theory reference.
- `src/theory/arithmetic/` owns the LRA tableau, model diversification,
  opportunistic equality producers, exact rational checks, and arithmetic
  explanations. [](arithmetic.md) is its theory reference.
- `src/sat/` owns the IPASIR-UP adapter, assumptions, observed variables,
  callback protocol, and failed-assumption queries. [](sat-ipasir-up.md)
  describes that boundary.

## Known limitations

- The coordinator is specific to EUF plus rational LRA and shared Real
  constants. It is not a reusable combination of an arbitrary number of
  theories.
- There is no support for quantifiers, nonlinear arithmetic, integer
  arrangements, arrays, bit-vectors, finite shared sorts, or non-convex theory
  combination in this path.
- Primo does not carry generic mutable component models in the DPLL(T) state.
  Its EUF and LRA objects remain theory-owned, while `SharedModelState` is a
  round-scoped projection.
- The Section 5.2 mutation implementation is scratch LRA mutation, not a
  theory-independent `M-Mutate` transition.
- Online LRA propagation recognizes the paired non-strict-bound shape only.
  Online EUF reuse remains experimental and default-off.
- Exact fallback can still inspect many model-equal shared pairs. The current
  model partition, opportunistic producers, and reconciliation frontier reduce
  that work but do not remove the worst-case quadratic pair space.
- Component preparation remains specialized and ground. Its output uses the
  general immutable `core::Model`, but the dedicated engine has not migrated
  to the collection driver's model-fragment export protocol.

Performance history, rejected experiments, and the current gap to reference
solvers belong in [](qf-uflra-model-guided-performance.md). The merge history
and remaining convex milestones are in [](qf-uflra-convex-no-merge-plan.md).
The isolation rule that keeps Nelson-Oppen work off pure LRA's hot path is
documented in [](nelson-oppen-qf-lra-regression.md), and the arithmetic equality
producers are detailed in [](qf-lra-equality-sharing.md).
