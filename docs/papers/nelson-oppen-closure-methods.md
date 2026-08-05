# Nelson-Oppen Closure Methods

## Status

This is a literature and design-intake note for the
`nelson-oppen-closure-methods` branch. It now also records the implemented
scaffolds: an SMT-owned `TheoryCombinationManager` for interface equality
bookkeeping, a compatibility read-only analyzer, and a partial mixed QF_UFLRA
coordinator with Real-valued UF-application purification, EUF-to-LRA proxy
equality checks, LRA-to-EUF equality checks over shared Real constants, and
model reconstruction for the supported Real-argument subset. It also records
the first SAT-callback combination slice: an opt-in
`MixedTheoryPropagationDispatcher` for `--nelson-oppen-prop=online` that
propagates shared Real equality from paired non-strict LRA bounds. It does not
claim that Primo implements the full Nelson-Oppen procedure yet.

The intended project is to add theory-combination closure mechanisms based on
Nelson-Oppen-style equality sharing, with de Moura and Bjorner's
model-based theory-combination paper as the first implementation target. Before
any source change claims to implement one of these algorithms, this note must be
expanded into an implementation checklist mapping the paper operations to Primo
C++ types, invariants, tests, and counters.

## Current Primo Baseline

Primo currently has QF_UF and QF_LRA DPLL(T) paths, EUF congruence closure, and
an LRA Simplex tableau. There is no general Nelson-Oppen combination layer yet.
The current branch adds a small mixed-theory coordinator for formulas that
remain separable after preprocessing, plus a first purified subset where
Real-valued UF applications occur inside arithmetic atoms.

Primo now has conservative pure-theory SMT-LIB `get-model` paths: QF_UF prints
definitions for user-declared UF constants, functions, and predicates from a
ground EUF closure, and QF_LRA prints user-declared nullary Real constants from
the final tableau assignment while composing in UF preprocessing reconstruction
facts when preprocessing has removed non-Real equalities. These are still
narrower than the model object needed for model-guided Nelson-Oppen
combination. `core::Model` remains a placeholder, mixed QF_UFLRA model printing
is still a narrow composition of a ground UF table and nullary-Real LRA
definitions, and
satisfiability-preserving preprocessing must emit explicit reconstruction
assignments when it removes user-visible facts.

This conservative model printer is a prerequisite for model-guided combination:
without a concrete model object, "model equality" can easily degrade into an
unexplained heuristic over the SAT assignment instead of the paper's operation
over candidate theory models.

The initial closure-method work therefore should not be framed as replacing an
existing general Nelson-Oppen module. It should introduce the minimum common
interfaces needed for future multi-theory cooperation while preserving the
existing ownership boundaries:

- `smt/` owns Boolean abstraction, theory atoms, assertion stacks, and DPLL(T)
  orchestration.
- `theory/` owns per-theory consistency, equalities, disequalities,
  propagation candidates, explanations, and model fragments.
- `sat/` sees only SAT literals and clauses.
- `core/` owns terms and sorts.

As of the current branch, `src/smt/theory_combination.cpp` implements the
first shared-equality ownership slice. `TheoryCombinationManager` scans the current
post-preprocessing assertions, records user-visible non-Boolean constants by
sort, records already-visible equality atoms whose two sides are such
constants, and builds a canonical same-sort interface equality registry. The
registry stores the SAT literal registered for each lazily generated equality
atom, a SAT assignment snapshot for registered equality literals, the current
combination-layer value of each registered pair (unknown, equal, or disequal),
and per-round classification flags such as EUF-implied, LRA-implied,
SAT-forced, assigned-false, or model/internal. The trailed values and assignment
state are backtrackable through `TheoryCombinationManager::push_level` and
`pop_to_level`; the manager owns only combination-layer equality state, not
theory-local closure/tableau state. For the mixed QF_UFLRA path, the manager is
also rebuilt from shared Real interface terms.
`analyze_nelson_oppen_interface` is a
compatibility wrapper around this manager, and the diagnostic path reports the
same information through `check_sat` statistics:

- `:no.interface.terms`
- `:no.interface.sorts`
- `:no.interface.candidate-pairs`
- `:no.interface.registry-entries`
- `:no.interface.visible-equalities`
- `:no.interface.missing-visible-equalities`

The opt-in diagnostic scan deliberately does not implement equality
propagation, model-equality guessing, care sets, purification, SAT backtracking
of arrangement values, or missing equality atom creation. It is a measurement
and API-shaping step. The scan is guarded by the explicit
`--nelson-oppen-diagnostics` CLI flag so ordinary benchmark runs do not pay for
extra interface traversal.

The mixed QF_UFLRA support in `src/smt/check_sat.cpp` is separate from that
diagnostic analyzer. `solve_qf_uf_lra` builds one SAT abstraction, partitions
the visible theory atoms into EUF and LRA atoms, and then performs final checks
against the accepted SAT assignment. An EUF conflict learns the ordinary EUF
explanation clause; an LRA conflict learns the ordinary LRA Farkas/bound
explanation clause.

The current runnable subset also purifies Real-valued uninterpreted
applications inside arithmetic atoms. After Real equality lowering, the SMT
layer replaces each such application with a fresh internal Real proxy and adds
a definition equality between the proxy and the UF application. That definition
is handled by EUF; the purified arithmetic atom is handled by LRA. The same
purification pass also handles non-constant Real arguments in EUF atoms: EUF
receives an internal Real proxy in place of the arithmetic argument, and LRA
receives the two defining bounds `proxy <= term` and `term <= proxy`.

When a theory first implies equality for a shared Real interface pair, the
coordinator creates a positive SAT-visible equality atom for that pair. During
the final mixed check, EUF-implied equalities between shared Real proxies and
LRA-implied equalities between shared Real constants are mapped to those
generated literals, creating the literal lazily if needed. If an implied
equality literal is not already true in the accepted SAT assignment, the
coordinator learns a guarded propagation clause and restarts the SAT loop. The
clause contains the generated equality literal plus the blocking literals for
the current assignment of the theory that implied it. This is intentionally less
minimal than a theory-local explanation, but it is sound for lazy final checks:
an equality discovered in one Boolean branch cannot become an unconditional
fact in another branch. `--nelson-oppen-prop=final` is this default guarded
final-check mode. `--nelson-oppen-prop=off` disables those guarded
equality-propagation clauses for ablations, while leaving ordinary per-theory
conflict checks and model reconstruction checks intact. Generated equality
literals that are true are asserted to both EUF and LRA. Generated equality
literals that are false are asserted to EUF as disequalities. They are not
asserted to LRA as native Real disequalities; instead, LRA can still reject such
a false assignment by later entailing the corresponding equality, and mixed
model reconstruction separately adds strict Real separation atoms for shared
Real arguments whose equality is rejected by the current EUF closure.

Default final-mode LRA-to-EUF sharing is now consumer-aware as a classifier.
When the LRA scan finds an entailed shared Real equality, `solve_qf_uf_lra`
first tests the current EUF closure that already contains current true
interface equalities and assigned-false generated interface equalities as
disequalities. EUF-already-true equalities are not materialized as SAT equality
atoms. Equalities that EUF can absorb are counted separately as mergeable, but
after the rejected 2026-08-05 full QF_UFLRA run they are conservatively forced
through the guarded SAT equality-clause path. This is still a QF_UFLRA-local
filter, not a generic model-based `M-Propagate` transition.

`--nelson-oppen-prop=online` adds a deliberately narrow online combination
path. In mixed QF_UFLRA only, `MixedTheoryPropagationDispatcher` is connected as
CaDiCaL's single external propagator and delegates ordinary LRA callbacks to
`OnlineLraPropagator`. The dispatcher tracks SAT assignments and manager
arrangement values on the SAT trail, but it no longer inspects LRA atoms for the
implemented rule. `OnlineLraPropagator` publishes an interface equality
candidate when its own trail contains both true non-strict bounds `(<= a b)` and
`(<= b a)` over a registered shared Real pair, with the two bound-blocking
literals as the explanation. The dispatcher maps that theory-owned candidate to
the positive SAT-visible interface equality literal and streams the LRA-provided
reason clause to CaDiCaL. If the equality atom is missing, the dispatcher
creates the equality term, registers it with `TheoryCombinationManager`,
allocates a fresh observed SAT variable with
`sat::IpasirUpBackend::new_observed_var()` from `cb_propagate()`, and records
the dispatcher mapping before returning the literal. The previous all-pairs
online precreation accommodation and the temporary paired-bound precreation
fallback have both been removed from this path:
`:no.online.precreated-equality-atoms` and
`:no.online.paired-bound-precreate-candidates` should remain zero, while
`:no.online.lazy-equality-atoms-created` and
`:no.online.lazy-equality-vars-observed` record selected candidate
materialization. `:no.online.shared-real-pairs` is only a diagnostic
cross-product count.

Online mode also skips the older final `lra_implied_real_equalities`
strict-bound scan over all shared Real pairs after it has materialized at least
one paired-bound equality candidate. If no candidate was materialized, the broad
scan still runs as the final-check completeness fallback. Default `final` mode
keeps that broad scan unchanged. If final arrangement validation or model
reconstruction fails after the skip, online mode runs the broad scan once as a
validation fallback and records
`:no.online.final-lra-equality-scan-fallbacks`. Those equalities are local to
the final validation/model step and are not reported as online LRA-to-EUF theory
propagations. This is a measured cost cap, not a more complete LRA equality
producer: arbitrary tableau-implied equalities remain unsupported by the online
producer, and cases that repeatedly need those equalities after paired-bound
materialization may still need the default final mode or a future capped/reused
equality check.

Default `final` propagation keeps the broad all-pairs scan unless the
experimental `--qf-uflra-candidate-final-scan` option is enabled. That option
builds a SAT-assignment-local candidate set from EUF care pairs, shared Real
constants that co-occur inside one EUF atom, and pairs whose two non-strict LRA
bounds are both true. Only those pairs are probed before LRA-to-EUF propagation.
If final arrangement validation or mixed model reconstruction fails after this
candidate scan, Primo runs the broad scan once and routes any newly discovered
LRA-implied equalities through `force_implied_interface_equalities()` before
accepting the arrangement. This is still a conservative implementation-local
pruning heuristic over entailed equalities, not the full paper transition system
with a mutable shared model object.

The paired-bound producer is deliberately the stable online LRA-to-EUF subset
for now. It is sound, has a compact two-literal explanation, and exercises the
combination path without making every LRA bound change search the tableau for
new shared equalities. A broader producer could look for tableau-implied
equalities, fixed offset rows, or other linear combinations that force two
shared Real terms to the same value, but that is an opportunity rather than a
current implementation target. Before such a producer is built, it needs a
separate design note and benchmark plan covering trigger conditions, duplicate
suppression, explanation construction, backtracking, and caps on the number of
candidate interface equalities created per SAT branch.

**Single-theory invariant.** `MixedTheoryPropagationDispatcher` is never used
for pure QF_UF or pure QF_LRA. Pure QF_UF keeps its direct EUF callback path;
pure QF_LRA keeps its direct `OnlineLraPropagator` path. The CVC-like
combination object is paid for only when the solver is actually combining EUF
and LRA in the mixed QF_UFLRA path.

With `--model-equality-branching`, the coordinator may also ask the LRA model
for same-sort shared Real pairs that currently have the same concrete value in
the LRA model. This is a default-off branching scaffold, not a unit propagation
rule: the equality atom is created as an optional SAT-visible atom, then kept on
an active IPASIR assumption trail while the coordinator tries further
model-equal pairs. Before creating the optional equality atom, the default-on
mutation filter for this branch mode checks whether adding either strict
separation, `x < y` or `y < x`, is still LRA-consistent under the current Boolean
assignment. If so, the current equality is treated as avoidable model choice and
the branch candidate is skipped. This is only a conservative instantiation of
the paper's mutation idea; Primo does not mutate the stored LRA tableau model.
If SAT rejects the active assumption set, Primo learns a blocking clause over the
failed assumptions reported by the SAT backend and drops those assumptions from
the active trail. If the receiving theory rejects an active equality, the
ordinary theory conflict learned under that assumption blocks the bad branch on a
later SAT call. A false optional equality is visible to EUF as a disequality but
is still not an LRA disequality. While optional model-equality assumptions are
active, any further EUF- or LRA-implied interface equalities are learned through
guarded assignment clauses, not as unconditional unit clauses.
`--model-equality-branch-policy=first` preserves the original full-pair scan
order. `ranked` prefers LRA-model-equal pairs that also appear in the EUF care
diagnostic, then pairs whose terms both occur as EUF arguments, then the
remaining full-pair fallback. `adaptive` uses the same ranking but stops adding
model-equality assumptions after repeated failed assumptions or theory conflicts
that did not produce interface equality propagation. `care-queued` is a
default-off pruning policy: it rebuilds a bounded candidate queue from the
current EUF care pairs only, filters already-tried, assigned, formula-occurring,
and mutation-avoidable candidates before creating a fresh equality atom, and
does not fall back to the full shared Real cross product.

This is sound only for the implemented partial subset. It rejects EUF atoms that
would require unsupported theories. Mixed `:produce-models true` supports UF
symbols with Real arguments, including non-constant arithmetic arguments after
purification, and UF symbols whose result sort is `Real`. The model path
augments the LRA reconstruction with EUF-implied Real-constant equalities and
strict disequalities separating EUF-distinct Real constants, including Real
constants that occur only as UF arguments, then gives those concrete Real values
to the UF model table printer for both guards and Real-valued results. The
coordinator publishes:

- `:solver.qf-uflra`
- `:no.mixed.euf-atoms`
- `:no.mixed.lra-atoms`
- `:no.purification.definitions`
- `:no.purification.arithmetic-uf-definitions`
- `:no.purification.euf-real-term-definitions`
- `:no.purification.reused-applications`
- `:no.purification.reused-real-terms`
- `:no.purification.memoized-formula-rewrites`
- `:no.purification.memoized-arithmetic-rewrites`
- `:no.purification.memoized-euf-rewrites`
- `:no.purification.skipped-no-sites`
- `:no.interface.shared-real-terms`
- `:no.interface.sat-equality-atoms`
- `:no.interface.sat-equalities-true`
- `:no.interface.sat-equalities-false`
- `:no.interface.sat-equalities-forced`
- `:no.interface.equalities-propagated`
- `:no.interface.euf-to-lra-equalities`
- `:no.interface.lra-to-euf-equalities`
- `:no.interface.lra-to-euf-equalities-euf-already-true`
- `:no.interface.lra-to-euf-equalities-euf-mergeable-skipped`
- `:no.interface.lra-to-euf-equalities-euf-mergeable-forced`
- `:no.interface.lra-to-euf-equalities-euf-forbidden-forced`
- `:no.interface.lra-to-euf-internal-model-equalities`
- `:no.interface.lra-to-euf-skip-model-reconstruction-fallbacks`
- `:no.interface.conflicts`
- `:no.interface.euf-conflicts`
- `:no.interface.lra-conflicts`
- `:no.interface.model-equality-candidates`
- `:no.interface.model-equality-created`
- `:no.interface.model-equality-pending`
- `:no.interface.model-equality-activated`
- `:no.interface.model-equality-duplicate-pending`
- `:no.interface.model-equality-assumptions`
- `:no.interface.model-equality-failed-assumptions`
- `:no.interface.model-equality-blocking-clauses`
- `:no.interface.model-equality-dropped-assumptions`
- `:no.interface.model-equality-retained-assumptions`
- `:no.interface.model-equality-conflicts`
- `:no.interface.model-equality-full-pairs`
- `:no.interface.model-equality-care-pairs`
- `:no.interface.model-equality-care-pruned-pairs`
- `:no.interface.model-equality-scanned`
- `:no.interface.model-equality-lra-equal`
- `:no.interface.model-equality-mutation-checks`
- `:no.interface.model-equality-mutated`
- `:no.interface.model-equality-mutation-forced`
- `:no.interface.model-equality-selected-care`
- `:no.interface.model-equality-selected-full`
- `:no.interface.model-equality-skipped-assigned`
- `:no.interface.model-equality-skipped-tried`
- `:no.interface.model-equality-queue-builds`
- `:no.interface.model-equality-queued`
- `:no.interface.model-equality-queue-pops`
- `:no.interface.model-equality-queue-skipped-tried`
- `:no.interface.model-equality-queue-skipped-assigned`
- `:no.interface.model-equality-queue-skipped-mutation`
- `:no.interface.model-equality-queue-empty`
- `:no.interface.model-equality-adaptive-stops`
- `:no.interface.model-equality-active-assumptions`
- `:no.interface.model-equality-max-active-assumptions`
- `:no.interface.arrangement-checks`
- `:no.interface.arrangement-failures`
- `:no.interface.arrangement-separations`
- `:no.interface.arrangement-lra-extra-atom-probes`
- `:no.interface.arrangement-lra-extra-atoms-probed`
- `:no.interface.lra-extra-atom-probes`
- `:no.interface.lra-extra-atoms-probed`
- `:no.interface.lra-extra-atom-probe-limit-hit`
- `:no.interface.euf-care-checks`
- `:no.interface.euf-care-full-pairs`
- `:no.interface.euf-care-pairs`
- `:no.interface.euf-care-pruned-pairs`
- `:no.interface.candidate-final-scan-enabled`
- `:no.interface.candidate-final-scan-pairs`
- `:no.interface.candidate-final-scan-skipped-pairs`
- `:no.interface.candidate-final-scan-equalities`
- `:no.interface.candidate-final-scan-fallbacks`
- `:no.interface.candidate-final-scan-fallback-equalities`
- `:no.interface.lra-prepared-reused`
- `:no.interface.lra-prepared-augmented`
- `:no.interface.model-separations`
- `:no.interface.model-separation-candidates`
- `:no.interface.model-separation-euf-mergeable-pairs`
- `:no.interface.model-separation-euf-forbidden-pairs`
- `:no.interface.model-separation-search-capped`
- `:no.interface.model-separation-search-branches`
- `:no.interface.model-separation-search-backtracks`
- `:no.interface.model-separation-model-oriented`

## Primary Base Paper

Leonardo de Moura and Nikolaj Bjorner. *Model-based Theory Combination.*
Electronic Notes in Theoretical Computer Science 198(2), 37-49, 2008.
DOI: <https://doi.org/10.1016/j.entcs.2008.04.079>.
SMT 2007 program entry:
<https://www.cs.upc.edu/~oliveras/smt07/home.html>.

### Relevance

The paper contrasts traditional Nelson-Oppen equality propagation with a
model-based alternative. Traditional combination either asks theory solvers to
produce all implied interface equalities or exposes the cross-product of shared
variable equalities to the SAT search. The paper's key implementation idea is to
inspect candidate theory models and propagate equalities that hold in those
models, then rely on backtracking if another theory disagrees.

For Primo this is relevant because:

- EUF can already build equivalence classes over ground terms.
- Future arithmetic and array solvers will need a shared-variable equality
  contract with EUF.
- Exhaustively enumerating shared-variable pairs risks creating too many SAT
  atoms before there is evidence that the equalities matter.
- Model-based guesses can be default-off while counters show whether they reduce
  Boolean search or only add churn.

### Implementation Questions For Primo

- Which terms are shared variables after purification: only named interface
  constants, or all purified proxy variables produced by the SMT layer?
- Which equalities must become SAT-visible atoms before they can be propagated?
- Can a theory solver request creation of a missing equality atom, or must
  closure initially be restricted to already-visible equality atoms?
- What model object is produced after `sat`, how does it reconstruct eliminated
  constants and preprocessing auxiliaries, and how is it printed as SMT-LIB?
- What is the explanation for a model-equality propagation: a theory-implied
  reason, a model-choice lemma, or a branch-causing guess that must be
  represented differently?
- How should model mutation be exposed: as a theory-local operation, a
  combination-layer heuristic, or a SAT phase/assumption strategy?
- What counters distinguish entailed equalities from model equalities, returned
  propagations, conflicts caused by disagreement, and discarded candidates?

### Likely First Subset

Historical note: this was the original staging plan. The current branch has
moved past part of it: the partial QF_UFLRA coordinator now runs in the default
final check, creates interface equality atoms lazily, and has an opt-in
model-equality branching mode with a strict-separation mutation filter. This is
still not the de Moura/Bjorner model-based combination algorithm. Primo does not
yet have a mutable model object shared by the theory solvers, complete
non-convex arrangement enumeration, or minimal paper-style explanations.

The first implementable subset should be split into two tracks. Track A is a
prerequisite for any model-guided closure claim:

1. Keep expanding explicit model construction beyond the current QF_UF subset.
2. Validate SMT-LIB `get-model` responses with Dolmen before relying on them in
   combination experiments.
3. Add model validation tests against the asserted formulas, including formulas
   affected by preprocessing passes.
4. Continue expanding reconstruction state for satisfiability-preserving
   rewrites beyond the current QF_UF unit and substitution subset.

Track B is the first closure scaffold, deliberately narrower than the full
paper. Experimental model-equality generation should remain default-off; the
implemented proxy-purification subset below is part of the default QF_UFLRA
path because rejecting those arithmetic atoms was the previous user-visible
gap:

1. Add a documented combination-interface skeleton that can list shared terms by
   sort and identify equality atoms over those terms.
2. For convex theories, support entailed equality propagation over existing
   SAT-visible interface equalities only.
3. Add counters and traces before adding model-equality guesses.
4. Add model-equality generation only for a theory solver that can provide a
   stable candidate model and a sound disagreement response.
5. Keep atom creation for missing interface equalities as a separate milestone,
   because it changes the Boolean abstraction contract.

Track C is the callback-safe model-based online refinement prompted by the
current profiling result:

1. The current implemented slice replaces online all-pairs equality precreation
   with lazy materialization of selected paired-bound candidates.
2. The dispatcher allocates and observes a fresh SAT variable from
   `cb_propagate()` only after the LRA producer publishes a candidate pair.
3. The dispatcher then returns only the observed equality literal for that
   selected candidate.
4. Future slices may add model-equality guesses that survive the mutation filter
   or a care-ranked equality source to the same candidate-first path.
5. Keep the full shared-pair cross product as a counter and fallback
   diagnostic, not as the set of observed SAT variables.
6. Online mode now skips the final `lra_implied_real_equalities` all-pairs
   strict-bound scan only after a paired-bound equality candidate has been
   materialized, because profiling showed this final scan can dominate after the
   online path adds more arrangement traffic. If the resulting final
   arrangement or model separation attempt fails, the broad scan runs once as a
   validation fallback and increments
   `:no.online.final-lra-equality-scan-fallbacks`. The reserved capped-scan
   counter remains zero until a capped replacement is implemented.
7. The default-off `--qf-uflra-candidate-final-scan` option applies the same
   candidate-first discipline to final-mode LRA-to-EUF equality sharing without
   changing default behavior. Its fallback broad scan is a validation escape
   hatch only; any fallback equality must be learned through the SAT-visible
   interface-equality clause path before the final arrangement is accepted.

### 2026-08-04 Model-Equality Assumption Lifecycle Checklist

**Bibliographic basis.** The branch lifecycle is based on de Moura and
Bjorner, "Model-based Theory Combination", Section 4, where a theory model may
suggest `u = v` for shared terms and the positive case is tried first, plus the
Section 5 observation that Simplex model values can be used to find equal
arithmetic terms. Section 5.2 motivates model mutation, but Primo currently
implements only the strict-separation consistency filter described below.

**Implemented subset.** `--model-equality-branching` remains default-off. In
mixed QF_UFLRA, `choose_lra_model_equality_branch` scans shared Real pairs for
the legacy `first`, `ranked`, and `adaptive` policies. The `care-queued` policy
uses `ModelEqualityCandidateQueue` instead: it rebuilds a queue from the current
EUF care pairs, caps it by the remaining branch budget, and consumes those
candidates without full-pair fallback. Both paths keep only pairs equal in the
current LRA model, apply the optional strict-separation mutation filter, create a
SAT-visible equality atom only for the selected pair, and set the SAT phase so
the positive equality is tried first. `ModelEqualityArrangementState` owns the
resulting assumption lifecycle:
one selected equality literal is queued, activated as an IPASIR assumption on
the next SAT call, retained across later SAT calls when it survives, and removed
when the SAT backend reports it as a failed assumption. SAT-level rejections
learn a blocking clause over the negated failed assumptions. EUF or LRA
conflicts reached while at least one guessed equality is active are learned by
the ordinary theory-conflict path.

**Data structures and owners.** `TheoryCombinationManager` owns the mapping
from equality term to SAT literal. `BooleanClausifier` creates the optional
theory atom when the equality is selected. `ModelEqualityCandidateQueue` owns
the care-pair candidate queue for the `care-queued` policy during the mixed
coordinator loop. `ModelEqualityArrangementState` owns the pending literal,
active IPASIR assumption trail, tried SAT variables, and adaptive rejection
counters. The SAT adapter still sees only literals and clauses; theory solvers
still see ordinary theory atoms and explanations.

**Invariants and operations.**

1. A model-equality guess is never reported as an entailed propagation.
2. A guessed equality remains an IPASIR assumption until SAT or a learned theory
   clause rejects it.
3. A SAT-level failed-assumption result must add a blocking clause containing
   only negated guessed literals.
4. The same SAT variable is not activated twice on the assumption trail.
5. Optional equality atoms are created lazily after the LRA model and mutation
   filter select a pair; shared Real pairs are not preallocated for this mode.

**Complexity expectation.** Candidate enumeration is still bounded by
`--model-equality-branch-budget` and the shared Real pair list for the legacy
policies. The `care-queued` policy avoids the broad candidate scan for branch
selection and rebuilds only from EUF care pairs. This is still a final-check
branching scaffold, not a generic model-propagation transition.

**Proof obligations.** The only clauses introduced by this subset are ordinary
theory conflicts, guarded interface-equality clauses from the existing
propagation path, and SAT failed-assumption blocking clauses over guessed
equalities. Because the guessed equality is an assumption, not a permanent unit,
the solver may backtrack and explore the negated arrangement.

**Deviations and limitations.** Primo does not implement the paper's full
`DPLL(T^M)` transition system, generic theory model object, paper-style
`M-Propagate` state, or real model mutation. The strict-separation mutation
filter asks whether either `x < y` or `y < x` is LRA-consistent under the
current assignment; when one is consistent, the model equality is treated as an
avoidable model choice and skipped. This is conservative and incomplete.

**Tests derived from the subset.** The QF_UFLRA unit tests cover a rejected
bad model-equality guess, mutation-filter skipping, full-pair fallback, active
assumption accumulation, care-ranked selection, ranked fallback, and adaptive
backoff. The tests assert lifecycle counters for queued and activated guesses,
and assert that mutation-filtered cases create no queued assumptions.

Track B can begin with the current QF_UF model printer if it is limited to
entailed equalities and interface bookkeeping. Track B must not implement or
advertise model-equality generation until Track A has produced a theory-facing
model object that theories can inspect, mutate, and validate. Even then, the
subset is not a complete implementation of the model-based theory combination
paper; it is the smallest scaffold that can be fuzzed and benchmarked without
claiming default readiness.

### Implemented Subset: Interface Bookkeeping

The implemented subset is intentionally below the algorithmic level of the
papers above. It is not de Moura/Bjorner model-based combination and not classic
Nelson-Oppen equality propagation. It provides the common data extraction needed
to make those future mechanisms measurable:

- **Data structures:** `TheoryCombinationManager`, `InterfaceTerm`,
  `InterfaceEquality`, `InterfaceEqualityRegistryEntry`,
  `InterfaceEqualityValue`, `PurificationSite`, and
  `NelsonOppenInterfaceSummary` in `src/smt/theory_combination.hpp`.
- **Owner:** `smt/`, because this layer owns post-preprocessing assertions and
  the Boolean/theory atom boundary.
- **Terms included:** user-visible constants of non-Boolean sorts that are
  reachable from the current assertions. Primo-generated `$primo_` auxiliaries
  and Boolean constants are excluded.
- **Equality atoms included:** existing equality terms whose two sides are
  distinct included interface constants of the same sort. Reversed duplicate
  pairs are canonicalized for counting. Polarity is intentionally ignored at
  this layer: an asserted disequality still makes the corresponding interface
  equality SAT-visible.
- **Registry entries:** every distinct same-sort interface-constant pair is
  represented as either `Visible` with the existing equality `TermId`, or
  `Missing` with an invalid atom id. Later theory-combination code can inspect
  this registry without guessing whether an equality atom already exists.
- **Arrangement values:** the manager stores `Unknown`, `Equal`, or `Disequal`
  for registered pairs. This is the first explicit combination-layer equality
  state. Current solver behavior only uses it in tests; future propagation code
  must connect these values to SAT assignment, theory explanations, and
  backtracking before relying on them for search.
- **Purification sites:** the analyzer records Real-sorted subterms that occur
  inside EUF atoms, and uninterpreted applications that occur inside arithmetic
  atoms. These sites are the places where a future purification pass would need
  to introduce owned proxy constants and equality-sharing obligations before the
  mixed atom can be delegated to one theory. The analyzer only reports the term,
  containing atom, site kind, and sort; it does not create those proxies.
- **Complexity:** when diagnostics are enabled, one traversal over reachable
  assertion terms plus per-sort sorting of discovered constants, followed by
  materializing the same-sort interface-pair registry and a hash index for
  canonical pair lookup. This remains opt-in because the pair registry is
  quadratic in the number of interface constants per sort. Purification-site
  collection is linear in the total visited subterms, modulo de-duplication of
  repeated `(kind, atom, term)` sites.
- **Invariant:** no term or SAT atom creation occurs here, so the analyzer is
  behavior-preserving and cannot change solver search.
- **Runtime status:** the analyzer is opt-in through
  `CheckSatOptions::nelson_oppen_diagnostics` and
  `--nelson-oppen-diagnostics`.
- **Statistics:** in addition to the interface-pair counters, the diagnostics
  publish `:no.purification.sites`, `:no.purification.euf-real-terms`, and
  `:no.purification.arithmetic-uf-apps`.
- **Tests:** `tests/unit/theory_combination_tests.cpp` checks sort isolation,
  visible equality detection, internal-symbol exclusion, Boolean exclusion,
  self-equality exclusion, reversed-pair deduplication, and a mock theory
  consumer that distinguishes visible equality atoms from missing pairs without
  creating atoms. It also checks canonical manager lookups, explicit
  unknown/equal/disequal arrangement values, cross-sort rejection, rebuild
  clearing, Real terms inside EUF atoms, and uninterpreted applications inside
  arithmetic atoms as purification sites.

Limitations:

- no purification ownership or proxy-term creation yet;
- no theory ownership labels beyond sort partitioning;
- no manager-level care-pair API;
- no SAT-backtracked arrangement trail;
- no explanations or learned clauses;
- no mixed-theory model object.

### Implemented Subset: Partial Mixed QF_UFLRA Final Checks

This subset is a conservative bridge, not the full Nelson-Oppen algorithm. It
exists so the branch can execute simple QF_UFLRA scripts without silently
treating one theory's atoms as Boolean noise.

- **Data structures:** `TheoryCombinationManager` owns the shared Real
  interface-pair registry, the generated equality atom `TermId`, the registered
  SAT literal, the current SAT assignment snapshot, the current
  unknown/equal/disequal arrangement value, and per-round classification flags
  for each pair. The final-check loop still reuses
  `BooleanClausifier::TheoryAtom`, `PreparedFlatTheory`, `PreparedLraTheory`,
  `AssignedTheoryAtom`, and ordinary SAT clauses. The purification pass uses
  SMT-local maps from original Real-valued UF application `TermId` values and
  non-constant Real argument `TermId` values to internal proxy `TermId` values.
  A memoized pre-scan detects when no such purification sites exist, in which
  case the pass leaves the assertion vector unchanged. When purification is
  needed, the rewrite pass also memoizes formula, arithmetic-term, and EUF-term
  rewrites over hash-consed `TermId`s. This is an implementation efficiency
  guard for the current proxy-purification subset, not the de Moura-Bjorner
  model-guided combination loop.
- **Owner:** `smt/check_sat.cpp`, because it owns the Boolean abstraction and
  solver dispatch.
- **Accepted shape:** EUF atoms over uninterpreted or Boolean terms, EUF
  predicate atoms whose Real arguments are shared Real constants, LRA atoms over
  linear Real terms, and arithmetic atoms containing Real-valued UF applications
  after purification.
- **Purification:** after Real equality lowering, each Real-valued UF
  application in an arithmetic term is replaced by a fresh `$primo_no_proxy_*`
  Real constant. Before the EUF definition equality is emitted, every
  non-constant Real argument of that application is also purified to a
  `$primo_no_real_arg_*` proxy, so the appended EUF-owned equality is over a
  flat UF application rather than over an arithmetic expression such as
  `f(x - y)`. The Real-argument proxy receives two LRA-owned bounds,
  `proxy <= term` and `term <= proxy`, instead of a Real equality, because the
  global `lower_real_equalities` pass has already run and because Real-valued
  UF application definitions must remain EUF-owned. The same Real-argument
  proxy mechanism is used for non-constant Real arguments in ordinary EUF atoms,
  recursively purifying any Real-valued UF applications inside the arithmetic
  argument.
- **Interface equality check:** generated equality atoms are created lazily,
  only when EUF or LRA implies equality for a shared Real-constant/proxy pair.
  When such a generated equality literal is true in the current SAT assignment,
  the equality is asserted into both EUF and LRA checks. When it is false, the
  disequality is asserted into EUF only; LRA disequality is still represented
  indirectly by the fact that LRA will force the equality if both strict
  orientations conflict. After both theory checks accept the current assignment,
  the EUF closure is inspected for implied equalities between Real proxy
  constants that also occur in LRA atoms. The reverse direction checks every
  same-sort shared Real pair by adding `x < y` and `y < x` to the current LRA
  assignment; if both conflict, LRA entails `x = y`. Either direction learns a
  guarded clause from the current theory assignment to the corresponding
  generated equality literal, and the next SAT iteration lets the receiving
  theory produce any ordinary conflict with that equality literal in its reason.
- **Reasoning loop:** in the default path, the original LRA atoms are also
  attached to CaDiCaL through Primo's existing trail-synced online LRA
  propagator, based on Dutertre and de Moura's simplex-style DPLL(T)
  propagation machinery documented in `docs/arithmetic.md`. This is not a new
  Nelson-Oppen algorithm: it only avoids repeatedly rediscovering base LRA
  conflicts after complete SAT assignments. After each `sat` result from
  CaDiCaL, the coordinator rebuilds/checks the EUF assignment and then either
  reuses the callback-owned base LRA result or rebuilds/checks an augmented LRA
  theory when true generated interface equalities must be included. EUF is
  augmented by currently true and false generated interface equalities; LRA is
  augmented by true generated equalities only. Add the first per-theory conflict
  clause found and continue. If both individual checks pass, learn guarded
  propagation clauses for any newly implied generated interface equality and
  continue. Before returning a non-model `sat`, validate that the final EUF
  arrangement over common purified Real interface constants can be separated
  consistently by the LRA side. The current implementation first tests whether
  temporarily merging a shared Real pair would make the EUF closure inconsistent;
  only such EUF-forbidden pairs enter the bounded strict-order search used for
  model reconstruction. Shared Real pairs that are merely in different EUF
  classes but are mergeable are not forced apart. Real constants that occur in
  EUF atoms but not in LRA atoms are counted by the pre-filter diagnostic only;
  they are not Nelson-Oppen arrangement variables. Return `sat` only when both
  theory checks accept the same Boolean assignment, neither theory contributes a
  new shared equality clause, and this final arrangement validation succeeds.
- **Model-equality branching:** if `--model-equality-branching` is enabled and
  neither theory forces an equality, the coordinator builds the current LRA
  model and classifies candidate pairs through the LRA-backed
  `theory::ModelEqualityEvaluation` path. The current adapter,
  `LraTheoryModelView`, reads the concrete LRA values and, when mutation
  filtering is enabled, asks whether either strict orientation is still
  LRA-consistent before a SAT equality atom is created. The default `first`
  policy keeps the historical full-pair order. The `ranked` policy keeps the
  full candidate set but tries care-diagnostic and EUF-argument pairs first. The
  `adaptive` policy uses the ranked order and stops issuing guesses after
  repeated unproductive rejected assumptions or theory conflicts. The
  `care-queued` policy scans only a bounded queue rebuilt from the current EUF
  care pairs, with no full-pair fallback. For a missing, user-invisible equality
  atom, the branch creates a SAT-visible optional atom only after the query
  reports an equal pair that was not filtered as mutation-avoidable, then
  retries the SAT loop with that atom on an active model-equality assumption
  trail. Surviving model-equal assumptions are
  re-applied while the coordinator tries later model-equal pairs.
  The equality is asserted to EUF/LRA when SAT assigns the atom true. If the SAT
  backend rejects the active assumption set, the coordinator learns a blocking
  clause over the failed assumptions and removes them from the active trail; a
  later false assignment is asserted to EUF as a disequality but still not to LRA
  as a native false-`Eq` prepared atom.
  Ordinary input-level Real disequalities are supported by the Real-equality
  lowering pass and Boolean-level case splitting. The branch does not force
  additional interface equalities as permanent units while the optional equality
  is active. The branch budget is controlled by
  `--model-equality-branch-budget`, and the selection policy by
  `--model-equality-branch-policy`.
- **Model status:** mixed `get-model` composes the QF_UF body and nullary-Real
  QF_LRA definitions. For common purified Real interface constants,
  reconstruction first adds EUF-implied Real-constant equalities to the LRA
  model tableau, then performs a bounded depth-first search over strict order
  atoms to separate Real constants whose equality is forbidden by the EUF
  arrangement and not forced equal by LRA. Each candidate orientation is
  accepted only if the augmented LRA assignment remains consistent. When the
  candidate set exceeds the current
  reconstruction cap, currently 18 candidate pairs, Primo records that fact and
  falls back to the former greedy orientation order instead of attempting
  exponential backtracking. This candidate-count cap does not by itself bound
  the total number of augmented LRA consistency probes. The opt-in
  `--nelson-oppen-arrangement-probe-limit` option is the soft work cap for
  temporary LRA probes in final LRA-to-EUF equality sharing and
  arrangement/model reconstruction; reaching it returns `unknown` and sets
  `:no.interface.lra-extra-atom-probe-limit-hit`.
  The resulting concrete Real values are used when printing UF tables for
  symbols with Real arguments and for Real-valued UF application results.
  Real-valued function tables use `0` as their default result and add entries for
  concrete applications whose LRA proxy value differs from that default. The
  same composed model builder is used for QF_UFLRA inputs that preprocess down
  to a pure LRA solve, so UF constants and UF equalities recorded as
  preprocessing reconstruction facts are still present in the printed model for
  the original assertions.
- **Tests:** `tests/unit/qf_lra_solver_tests.cpp` covers a separable satisfiable
  mix, EUF-conflict learning, LRA-conflict learning, separable mixed
  `get-model`, shared-Real predicate `get-model` with a separating LRA repair,
  shared-Real predicate `get-model` when LRA forces equality, Real-valued UF
  application models, bounded shared-Real model-separation search over multiple
  candidates, positive SAT-visible interface equality forcing in both LRA-to-EUF
  and EUF-to-LRA directions, guarded interface-equality propagation that must not
  leak an equality from one Boolean branch into another, purification of
  non-constant Real arguments to EUF predicates with proxy reuse and model
  printing, purification of Real-valued UF applications in arithmetic atoms, an
  EUF-implied proxy equality that makes the LRA side inconsistent, an
  EUF-implied proxy equality combined with a lowered input Real disequality,
  LRA-implied shared Real-constant equality that makes the EUF side inconsistent,
  source-split interface conflict counters for those receiving-theory conflicts,
  closure-aware EUF care-pair counters over shared Real arguments, use of the
  full shared Real pair set for LRA-to-EUF equality propagation even when the
  EUF care diagnostic reports a pair as pruned, default-off model-equality
  guessing with both the historical full-pair order and the ranked care-first
  policy, adaptive model-equality backoff, query-layer counters for
  mutation-disabled and mutation-filtered model equalities, a non-unary
  regression where equality of both binary-predicate arguments is needed for an
  EUF conflict, final
  arrangement validation before a non-model `sat` result,
  and a regression where default preprocessing solves away a UF equality before
  the remaining arithmetic formula dispatches to the LRA solver. It also covers
  opt-in model-equality branching over an LRA-model equal pair that EUF rejects,
  forcing the solver to back off and still return `sat`.

Limitations:

- purification is limited to Real-valued UF applications in arithmetic atoms
  and non-constant Real arguments in EUF atoms;
- shared-variable equality propagation is final-check only;
- generated interface equalities are lazy: unentailed shared pairs do not create
  SAT atoms, and false generated equality literals are asserted only to EUF, not
  as native false-`Eq` LRA atoms;
- EUF care-pair tracking is closure-aware over flat EUF
  definitions and pairs shared Real arguments whose immediate flat function
  prefixes are equal in the current EUF closure, which covers curried non-unary
  applications after earlier argument equalities have been asserted. It does
  not filter LRA-to-EUF equality propagation, final arrangement validation, or
  model reconstruction, and default solving skips the scan when no
  model-equality branch consumer is enabled. It is used by the `ranked` and
  `adaptive` model-equality branch policies only as an ordering hint with
  full-pair fallback. It does not add clauses and is not an implementation of
  the Jovanovic-Barrett care-set algorithms. The current scan is quadratic in
  the number of shared-argument flat definitions, which is intentionally simple
  and remains behind default-off model-equality branching for active consumers;
- cross-theory equality propagation uses guarded clauses over generated equality
  literals, but still lacks the minimal explanation machinery of a full
  Nelson-Oppen implementation;
- opt-in model-equality branching is an active-assumption loop over
  LRA-model-equal shared Real pairs only. It now has a generic
  model-equality-result type and an LRA adapter, but still is not the full de
  Moura/Bjorner algorithm because no mutable theory-model object or
  explanation-producing model-equality propagation exists;
- Real-argument model reconstruction backtracks only up to a fixed candidate
  cap, currently 18 pairs, then records
  `:no.interface.model-separation-search-capped` and falls back to greedy
  orientation; final arrangement validation reuses the same bounded search and
  greedy fallback;
- no arrangement enumeration for non-convex combinations;
- mixed-theory model reconstruction is limited to the documented QF_UFLRA
  purification subset.

## Core Nelson-Oppen Sources

Greg Nelson and Derek C. Oppen. *Simplification by Cooperating Decision
Procedures.* ACM Transactions on Programming Languages and Systems 1(2),
245-257, 1979. DOI: <https://doi.org/10.1145/357073.357079>.

Derek C. Oppen. *Complexity, Convexity and Combinations of Theories.*
Theoretical Computer Science 12(3), 291-302, 1980.
ScienceDirect entry:
<https://www.sciencedirect.com/science/article/pii/0304397580900596>.

Cesare Tinelli and Mehdi T. Harandi. *A New Correctness Proof of the
Nelson-Oppen Combination Procedure.* FroCoS 1996, pp. 103-120.
DOI: <https://doi.org/10.1007/978-94-009-0349-4_5>.

### Relevance

These are the correctness baseline: purification, disjoint signatures, shared
variables, arrangements, stable infiniteness, convexity, and equality sharing.
They define the soundness obligations that any optimized or model-based closure
method must preserve.

### Primo Checklist Items

- Define purification ownership in `smt/` and ensure theory solvers never see
  mixed-theory terms.
- Maintain a sorted shared-variable set and avoid equality sharing across
  incompatible sorts.
- Document whether each theory is stably infinite and convex for the fragment
  being combined.
- For non-convex theories, do not claim classic deterministic Nelson-Oppen
  completeness unless disjunctions of interface equalities or an equivalent SAT
  search mechanism are implemented.

## Solver Architecture Sources

Clark W. Barrett, Christopher L. Conway, Morgan Deters, Liana Hadarean, Dejan
Jovanovic, Tim King, Andrew Reynolds, and Cesare Tinelli. *CVC4.* CAV 2011.
PDF: <https://smt-comp.github.io/2012/system-descriptions/cvc4.pdf>.

Tim King. *Effective Algorithms for the Satisfiability of Quantifier-Free
Formulas Over Linear Real and Integer Arithmetic.* PhD thesis, New York
University, 2014. In particular, Section 1.5.5 describes the CVC4
`THEORYENGINE` as the layer that dispatches literals to theory solvers and
coordinates combination.
PDF: <https://cs.nyu.edu/media/publications/king_tim.pdf>.

Haniel Barbosa, Clark Barrett, Martin Brain, Gereon Kremer, Hanna Lachnitt,
Makarios Mann, Abdalrhman Mohamed, Mudathir Mohamed, Aina Niemetz, Andres
Nötzli, Alex Ozdemir, Mathias Preiner, Andrew Reynolds, Ying Sheng, Cesare
Tinelli, and Yoni Zohar. *cvc5: A Versatile and Industrial-Strength SMT
Solver.* TACAS 2022. PDF:
<https://www-cs.stanford.edu/~preiner/publications/2022/BarbosaBBKLMMMN-TACAS22.pdf>.

cvc5 option documentation for equality-engine mode:
<https://cvc5.github.io/docs/cvc5-1.0.9/options.html>.

Bruno Dutertre. *Yices 2.2.* CAV 2014. PDF:
<https://yices.csl.sri.com/papers/cav2014.pdf>.

### Relevance

These sources support the architecture choice: the SMT engine owns the
combination protocol, while individual theory solvers keep theory-local data
structures and report conflicts, propagations, and model fragments through a
controlled interface. cvc5 is especially useful because it explicitly separates
the Combination Engine from theory solvers and supports both distributed and
central equality-engine modes. Primo should start closer to the distributed
mode: keep EUF congruence closure and LRA arithmetic state local, but let
`TheoryCombinationManager` own shared/interface terms, interface equality atoms,
and the combination-layer arrangement over those pairs.

The central-equality-engine option in cvc5 is a useful future experiment, not
the current target. Making every theory depend on one global congruence closure
would couple theory internals before Primo has arrays, datatypes, bit-vectors,
or strings. The conservative CVC-like target is therefore a manager in `smt/`
that maintains the equality contract across theories without replacing
theory-local equality machinery.

Care graphs are not required merely because the manager exists. If Primo
follows a Z3/de Moura-Bjorner-style model-based route, the first candidate pairs
can come from delayed SAT-visible equality creation and model disagreement
rather than from a full care-graph API. Care information remains important as a
scalability filter: without some relevance test, all same-sort shared pairs can
still become quadratic noise. The current EUF care-pair scan is a narrow
consumer-driven filter, not the architectural foundation.

### Primo Checklist Items

- Keep `TheoryCombinationManager` in `smt/`; it may store SAT-visible equality
  atoms and arrangement values, but it must not inspect theory-private tableau,
  congruence-closure, array, datatype, or bit-vector internals.
- Keep theory-local equality data structures in `theory/`; theories report
  propagated equalities, conflicts, and model facts through explicit APIs.
- Make any future central-equality-engine mode an option with separate tests and
  counters, not an implicit dependency of every theory.
- Treat care pairs as a pruning interface that a theory can provide when there
  is a real consumer. Do not require full care graphs before implementing
  model-based equality branching.
- Before using manager arrangement values in search, add a SAT-level
  backtracking trail and explanation path for each `Equal` or `Disequal` value.
  The current branch implements that trail for SAT-visible interface equality
  values owned by `TheoryCombinationManager`.
- Do not route pure single-theory solving through the combination dispatcher:
  QF_UF and QF_LRA must keep their direct callback paths unless a second theory
  is present.

## DPLL(T) Integration

Sava Krstic and Amit Goel. *Architecting Solvers for SAT Modulo Theories:
Nelson-Oppen with DPLL.* FroCoS 2007, LNCS 4720, pp. 1-27.
DOI: <https://doi.org/10.1007/978-3-540-74621-8_1>.

### Relevance

This paper gives a transition-system view of SMT solvers that combine a SAT
engine with multiple disjoint theories. It is directly useful for Primo because
Primo already has a CDCL backend through CaDiCaL and a DPLL(T)-style outer SMT
loop. The paper helps specify which events belong to SAT assignment, literal
dispatch, equality dispatch, theory inference, explanation, learning, and
backjumping.

### Primo Checklist Items

- Represent theory ownership and shared-literal dispatch explicitly instead of
  letting one theory inspect another theory's atoms.
- Keep learned combination clauses as ordinary SAT clauses with theory-owned
  explanations.
- Add tests where equality inferred by one theory must be visible to another
  before the SAT model can be accepted.
  Current coverage includes the implemented online LRA-bound equality rule; it
  does not yet cover a general model-guided equality producer.
- For IPASIR-UP/CaDiCaL integration, do not confuse an internal candidate queue
  with a legal SAT propagation queue. A returned propagation literal and every
  literal in a current external clause must be observed under the backend
  contract Primo currently targets.

## Delayed And SAT-Mediated Combination

Roberto Bruttomesso, Alessandro Cimatti, Anders Franzen, Alberto Griggio, and
Roberto Sebastiani. *Delayed Theory Combination vs. Nelson-Oppen for
Satisfiability Modulo Theories: A Comparative Analysis.* LPAR 2006, LNCS 4246,
pp. 527-541. PDF:
<https://es-static.fbk.eu/people/griggio/papers/lpar06_dtc.pdf>.

Diego Caminha B. de Oliveira, David Deharbe, and Pascal Fontaine.
*Combining Decision Procedures by (Model-)Equality Propagation.* Science of
Computer Programming 76(12), 1070-1082, 2011. PDF:
<https://people.montefiore.uliege.be/pfontain/Caminha4.pdf>.

### Relevance

Delayed theory combination shifts some or all interface-equality search to the
SAT solver by making the relevant equality choices Boolean. Model-equality
propagation is close in spirit to de Moura/Bjorner: it exchanges equalities
deduced by theories or generated by theory models, with SAT responsible for
resolving bad guesses.

These papers are implementation-relevant because Primo already has a SAT-backed
theory atom abstraction. The risk is that adding all shared-pair equalities
eagerly may dominate parsing, clausification, and SAT preprocessing, exactly the
kind of overhead seen in earlier QF_UF experiments when a mechanism was active
but had little delivered propagation. The current QF_UFLRA default `final` path
follows the first checklist item below by checking candidate equalities
separately from lazy SAT atom creation. The opt-in online paired-bound path does
not precreate the full shared-pair cross product. With the backend-neutral
IPASIR-UP boundary, it also avoids the temporary paired-bound precreation
fallback: the dispatcher creates and observes a SAT equality literal only for a
selected published candidate.

### Primo Checklist Items

- Separate candidate generation from SAT atom creation.
- Measure shared-pair count, created equality atoms, propagated interface
  equalities, and conflicts caused by interface disagreement.
- Keep eager all-pair equality creation out of both the default path and the
  opt-in online paired-bound path. Current `--nelson-oppen-prop=online`
  precreates no equality atoms; it lazily creates and observes equality atoms
  only for selected paired-bound candidates. Model/care candidates remain future
  work.
- Keep a delayed strategy available as an ablation baseline against
  model-based closure.

## Care Sets And Reduced Equality Sharing

Dejan Jovanovic and Clark Barrett. *Sharing is Caring: Combination of Theories.*
FroCoS 2011, LNCS 6989, pp. 195-210. PDF:
<https://cvc4.github.io/publications/2011/JB11-FroCoS.pdf>.

Dejan Jovanovic and Clark Barrett. *Being Careful About Theory Combination.*
Formal Methods in System Design, 2012/2013. PDF:
<https://cvc4.github.io/publications/2013/JB13.pdf>.

### Relevance

These papers reformulate Nelson-Oppen-style combination around explicit
equality propagation and "care" information, so solvers can ignore pairs of
shared variables that cannot affect their current reasoning. This is highly
relevant to Primo because a naive shared-variable cross-product can be too large
and because the QF_UF branch has repeatedly shown that generated work must be
distinguished from delivered, useful work.

### Primo Checklist Items

- Add a theory API for care pairs only after there is a concrete consumer.
- For EUF, derive care pairs from terms whose congruence result can change if an
  argument equality changes.
- Track care-pair pruning counters and compare them with full cross-product
  delayed-combination counters.

Primo currently implements the last checklist item only. The mixed coordinator
counts the full shared-Real cross product and the subset of pairs that appear as
arguments below flat EUF function prefixes equal in the current EUF closure, but
that subset is diagnostic. LRA-to-EUF equality checks, default-off
LRA-model-equality branch candidates, final arrangement validation, and mixed
model reconstruction all consider the full set of shared Real constants. A
care-pair API should be added only after there is a theory-facing consumer with
clear correctness and benchmark evidence.

## Broader Combination-Property Context

Clark Barrett, Roberto Sebastiani, Sanjit Seshia, and Cesare Tinelli.
*Satisfiability Modulo Theories.* Handbook of Satisfiability, 2009.
Author PDF: <https://people.eecs.berkeley.edu/~sseshia/pubdir/SMT-BookChapter.pdf>.

Guilherme V. Toledo, Yoni Zohar, and Clark Barrett. *Combining Combination
Properties, Part I: Nelson-Oppen and Politeness.* Journal of Automated
Reasoning, 2026. DOI: <https://doi.org/10.1007/s10817-025-09746-5>.

### Relevance

The handbook chapter is useful as a survey of the SMT setting and terminology.
The 2026 combination-properties paper is not an immediate implementation target,
but it is a useful guardrail when we later consider non-stably-infinite or
finite-model theories. It should remain background unless the branch explicitly
targets polite or finite-model combination.

## Initial Design Direction

The first design should be conservative:

- Handle explicit model construction and model printing before implementing
  model-equality guesses from de Moura/Bjorner.
- Do not add eager all-pairs equality atoms as the default.
- Keep growing `TheoryCombinationManager` as the SMT-owned coordinator for
  shared terms, equality atoms, arrangement values, and theory ownership, while
  delegating theory reasoning to `theory/`.
- Start from already-visible equality atoms and lazy generated equality atoms,
  then measure whether broader missing-atom creation is needed.
- Add separate counters for `candidate`, `created`, `entailed`, `model`,
  `returned`, `conflict`, and `discarded` interface equalities.
- Require focused tests for purification, sort isolation, backtracking,
  equality dispatch, conflict explanations, and model-equality disagreement.
- Require a QF_UF-plus-second-theory benchmark or synthetic mixed-theory corpus
  before any performance claim.

The most plausible competitive path is not "full Nelson-Oppen closure
everywhere"; it is a staged combination layer that starts with precise
interface-equality propagation and lazy equality-atom creation, then tries
model-based equality guesses where a theory can cheaply produce candidate
models. Care pairs should be added where they have a concrete consumer and
measured against the unfiltered candidate set; they are a scalability tool, not
a requirement for every model-based combination experiment.
