# DPLL(T): connecting Boolean search and theory reasoning

The [SAT chapter](sat.md) explains how assertions become clauses. Those clauses
describe the Boolean structure, but do not by themselves enforce equality,
arithmetic, or agreement between theories. DPLL(T) connects Boolean search to
those obligations through explained conflicts and consequences.

Primo delegates decisions, Boolean propagation, conflict analysis, learning,
and backjumping to CaDiCaL. Its SMT drivers translate SAT assignments into
theory facts, schedule theory checks, and decide whether a candidate can become
a solver answer. There is no single C++ driver for every logic. This chapter
describes their common contract and the differences that affect correctness;
congruence closure, arithmetic, and combination algorithms need their own
chapters.

## Representation and ownership

An engine receives assertion `TermId`s, options, model-conversion records, and
a live term manager. Its result is a {source}`CheckSatOutcome
<src/smt/check_sat.hpp#L233-L240>`, not a raw SAT assignment. The
[architecture chapter](architecture.md) places engine selection and result
adaptation around this boundary.

| State | Owner and representation |
| --- | --- |
| SAT trail and clause database | CaDiCaL behind {source}`CadicalBackend <src/sat/cadical_backend.hpp>`. Neither SAT literals nor the backend contain core terms. |
| Primo's observed assignment | {source}`AssignmentCoordinator <src/smt/assignment_coordinator.hpp>` is the sole writer of {source}`sat::Trail <src/sat/trail.hpp>`. Consumers read {source}`sat::TrailView <src/sat/trail_view.hpp>`; truth comes from the trail's variable-to-position index. |
| Meaning of SAT variables | {source}`BooleanClausifier <src/smt/clausifier.hpp#L20-L57>` owns the atom registry and term-to-literal map, borrowing the backend and term manager. |
| Dedicated EUF callback state | {source}`OnlineEufPropagator <src/smt/solver_engines.cpp#L1638-L1690>` borrows the atom vector, terms, options, and statistics; owns atom-index metadata, candidate queues, reasons, and optional incremental closure state; reads the shared assignment view. |
| Dedicated arithmetic callback state | {source}`OnlineLraPropagator <src/smt/solver_engines.cpp#L7066-L7107>` owns a mutable tableau copied from prepared structural state, kernel checkpoints, queues, and reasons; reads the shared assignment view and borrows the prepared data, atom vector, backend, and statistics. |
| Component routing | {source}`ComponentDispatcher <src/smt/component_dispatcher.hpp#L23-L77>` owns variable routes, temporary assignment deltas, stream cursors, and explanation clauses; it borrows component pointers. |
| Component facts and outcomes | {source}`AssignedTheoryLiteral and TheoryCheckResult <src/theory/theory_solver.hpp>` carry a core atom, its SAT literal/value, clauses, consequences, model stamps, or a typed stop result. {source}`TheoryComponent <src/theory/theory_component.hpp#L88-L130>` owns theory-local execution behind this contract and receives no SAT backend handle. |

Copying a `TermId` or `sat::Lit` does not extend its owner's lifetime. Registration
and callbacks can grow vectors/maps, so borrowed entries must not survive those
mutations. The [core ownership rules](core.md) still apply. Connecting a
propagator does not transfer ownership to SAT; callbacks must stop before their
targets are destroyed or their routing is replaced. The {source}`AssignmentConnection guard <src/smt/assignment_coordinator.hpp>`
disconnects with the consumer and shared view still alive, then unbinds the view.
The component driver uses it at
{source}`its detached preparation boundary <src/smt/component_solver.cpp>`.

## Follow an assignment through the callbacks

The callback protocol is defined by {source}`ExternalPropagator
<src/sat/ipasir_up.hpp#L39-L60>` and implemented through the
{source}`CaDiCaL adapter <src/sat/cadical_backend.cpp#L35-L85>`.
Only observed variables reach theory assignment routing. Their notifications
may be batched; a callback must reason over the notified trail, not assume a
fresh independent model each time.

1. **Receive assignments.** A signed integer reports which literal became
   true. The coordinator rejects invalid/opposite events, ignores identical
   duplicates, updates the shared view, then delivers the new literals. {source}`EUF routing <src/smt/solver_engines.cpp#L2068-L2100>` looks up
   its atom and updates the incremental state
   when present. {source}`ComponentDispatcher::notify_assignment
   <src/smt/component_dispatcher.cpp#L58-L88>` batches deltas by owner and marks
   affected components dirty. Optional relevance filtering is an SMT policy;
   it is not a license for a theory to ignore arbitrary assigned constraints.
2. **Open a SAT decision level.** The coordinator saves the trail boundary.
   EUF records a kernel level, creating its closure checkpoint lazily through {source}`apply_new_decision_level
   <src/smt/solver_engines.cpp#L1796-L1801>`. The component dispatcher tells
   every component to `push_level`, even when the SAT decision itself names
   no theory atom. SAT levels are not SMT-LIB assertion scopes.
3. **Check or propagate.** A conflict supplies a clause falsified by the
   current trail. A propagation supplies a literal with an explanation.
   In the component route, {source}`partial_check
   <src/smt/component_dispatcher.cpp#L145-L183>` visits dirty components;
   components marked `check_on_full_model` wait for a complete assignment.
   These checks use `Partial` effort even inside the model callback.
4. **Let SAT react.** CaDiCaL uses the explanation for conflict analysis or
   propagation and selects the resulting backjump. Primo supplies theory
   reasons, not its own CDCL implementation.
5. **Undo assignments.** The coordinator removes the suffix and supplies its
   exact literals after updating the view. EUF's {source}`apply_backtrack
   <src/smt/solver_engines.cpp>` restores closure state, removes discarded
   reasons, and clears pending candidates/conflicts. The
   {source}`component dispatcher <src/smt/component_dispatcher.cpp>` similarly
   prunes reasons and pops each component's kernel to the reported level.
   Root and retained-level facts survive; same-level rollback is a no-op.

Routing a batch is linear in its literal count plus the receiving components'
work. Streaming a clause costs one callback per literal plus a terminator.
Neither observation bounds the cost of a theory check, explanation, rollback,
or the full search. Dense SAT-variable arrays also consume space up to their
largest registered index, including gaps.

### Assignment rollback regression

The audit found that EUF initialized its decision boundaries with an extra
zero entry. At the first decision, `r` root literals therefore produced
boundaries `{0, r}`; backtracking to zero cleared their assignment markers,
while the closure's separate
{source}`checkpoints <src/smt/solver_engines.cpp#L1365-L1393>` preserved the facts.
The repair in `ac2aaaf` removed the synthetic root boundary. The shared
{source}`trail now owns this rule <src/sat/trail.cpp>`: entry `k` marks the
start of level `k+1`, and rollback keeps exactly `new_level` boundaries.

Dedicated {source}`callback tests <tests/unit/euf_callback_tests.cpp>` drive the production
callback through the EUF component adapter, dispatcher, and shared coordinator. Three traces reproduced
lost transitive-equality propagation before the repair: root retention,
retained-level retention, and repeated backtracks with empty observed levels.
All five groups pass after the repair. Conflict and exported-model-fact controls
pass both before and after; returned explanations are independently checked
against every interpretation of the fixture's five constants. No incorrect
SAT/UNSAT answer was reproduced. Deferred callbacks during a borrowed model
scope and every combined-theory schedule are outside these direct tests.

### Shared assignment ownership

The SAT API reports notifications and a backtrack level, so Primo retains one
mirror of the observed assignment per connection. No backend-native trail
extension is required. The dedicated EUF/arithmetic, mixed, component, and
quantified-UF consumers no longer own mutable truth arrays or assignment
boundaries. Theory-specific merge/bound undo logs remain necessary.

{source}`ComponentAssignmentView <src/theory/component_assignment.hpp>` stores
atom-registration metadata and projects values from the shared view. Full
component checks build temporary snapshots; quantifiers order their projection
by SAT trail position to preserve scheduling. Lazy routes load a completed-model
snapshot through the same read interface, without enabling online propagation.
The mixed route registers newly generated interface atoms before each solve,
including when only arithmetic uses online callbacks. The interface manager
reads their assignment from the view; its tentative model-equality values are
separate theory state.

Online model callbacks validate notified state instead of filling missing
assignments. Retained sessions reconnect with a new owner and re-observe root
facts after the preceding connection has settled its reason callbacks.
{source}`Protocol tests <tests/unit/assignment_coordinator_tests.cpp>` cover
root/retained levels, empty levels, duplicate and invalid events, growth,
reentrancy, protected replay, and reconnect. Route qualification and baseline
comparison are still underway; the previous rollback fuzz gate does not qualify
this migration.

## Conflicts, propagations, and explanations

For currently true theory literals `l₁, …, lₖ`:

| Theory result | Clause supplied to SAT |
| --- | --- |
| Their conjunction is inconsistent | `¬l₁ ∨ … ∨ ¬lₖ` |
| They imply an unassigned literal `q` | `¬l₁ ∨ … ∨ ¬lₖ ∨ q` |

These clauses must follow from the theory and any correctly represented
premises. A model preference such as “these two terms happen to have equal
values” is not an entailed equality. Such choices need assumptions or an
explained case split, not unconditional propagation.

In the component API, `TheoryCheckResult::reasons` contains **true antecedents**.
{source}`implication_clause <src/smt/component_dispatcher.cpp#L18-L24>` negates
them when building a SAT clause. Dedicated propagators instead retain clause
tails already carrying the required signs. Confusing the two conventions
would reverse the meaning of an explanation.

{source}`EUF reason storage <src/smt/solver_engines.cpp#L2237-L2276>` checks
that a candidate reason contains its conclusion exactly once and that its
other literals are currently false and observed. This is a shape/trail check,
not an independent proof of theory validity. The
{source}`reason callback <src/smt/solver_engines.cpp#L1922-L1947>` streams the
conclusion and clause tail, ending with zero. The component route similarly
{source}`stores a reason before returning a propagation
<src/smt/component_dispatcher.cpp#L207-L245>`.

Reason delivery can be delayed until SAT asks for it. That does not mean the
reason is always computed lazily: a propagator may already have built and
stored it. Delayed computation must preserve a valid dependency on the trail
at propagation time; explaining a literal using a consequence derived later
from that same literal can create a cycle in conflict analysis.

### Reason lifetime is separate from clause retention

A reason for a propagation that survives backtracking may still be requested.
The {source}`LRA rollback <src/smt/solver_engines.cpp#L7115-L7141>` and component
dispatcher erase reasons for assignments actually removed, preserving earlier
ones. Pending, undelivered candidates may be discarded and rediscovered; live
reasons are not interchangeable with that queue. The existing
{source}`root-reason lifetime regression
<tests/unit/component_combination_tests.cpp#L526-L579>` checks this distinction.

The dedicated {source}`EUF conflict callback
<src/smt/solver_engines.cpp#L1949-L1973>` and
{source}`LRA conflict callback <src/smt/solver_engines.cpp#L7344-L7354>` request
non-forgettable external clauses in the API variant carrying that flag.
In contrast, {source}`ComponentDispatcher::cb_has_external_clause
<src/smt/component_dispatcher.cpp#L247-L255>` sets it to **true**. Do not describe
all Primo theory conflicts as permanently retained. Backend retention of a
requested reason, a propagator's C++ storage, and SAT's learned backjump clause
are three separate things. The [Yices note](../papers/yices.md) and
[forgetting investigation](../leads/theory-lemma-forgetting.md) cover the
remaining policy questions; this chapter does not establish actual deletion
timing or a global termination proof.

## Lazy and online routes

The EUF engine choice and propagation schedule are separate options. The
{source}`defaults <src/smt/check_sat.hpp#L243-L250>` select incremental EUF with
automatic scheduling; {source}`mode resolution
<src/smt/solver_engines.cpp#L483-L552>` chooses a schedule for the goal.
Use the actual {source}`CLI names <src/main.cpp#L630-L634>`:
`--euf-engine` and `--euf-prop`.

| Route | During SAT search | After a SAT candidate |
| --- | --- | --- |
| EUF `--euf-engine=lazy --euf-prop=off` | No EUF external propagator | Build/check the selected theory assignment; add a conflict clause and solve again when inconsistent. |
| Incremental EUF with `--euf-prop=conflict` | Maintain the closure and deliver conflicts; do not return positive theory propagations through `cb_propagate` | Reuse the synchronized closure's conflict verdict. |
| Incremental EUF with `--euf-prop=online` | Maintain the closure, deliver conflicts, and attempt budgeted explained propagation | Reuse that closure's verdict. Propagation is not exhaustive. |
| EUF `--euf-prop=model` | No online propagator in the dedicated UF driver | Attempt propagation lemmas from the full assignment, and perform the selected final consistency check. |
| Dedicated LRA with CaDiCaL | Maintain a backtrackable tableau; finish deferred repair through the external-clause callback | Reuse the tableau conflict state; extract a model if requested. Turning off optional bound propagation does not turn off consistency checking. |
| Online component recipe | Route assignments and partial checks through `ComponentDispatcher` | Inspect pending lemmas/failures, then perform full component checks and shared-model reconciliation. |

The owning loops are {source}`solve_qf_uf_engine_impl
<src/smt/solver_engines.cpp#L10534-L10748>`,
{source}`solve_qf_lra_impl <src/smt/solver_engines.cpp#L8320-L8461>`, and
{source}`solve_component_recipe <src/smt/component_solver.cpp#L75-L467>`.
“Lazy” above describes when the theory check runs; it does not describe lazy
reason construction or scoped incremental sessions.

## When a candidate becomes an answer

`backend.solve() == Sat` means the backend has satisfied the current Boolean
clauses and its callback obligations. The SMT driver still owns the remaining
theory, combination, and result-construction obligations.

- **Dedicated incremental EUF:** the driver reuses the callback closure when
  present; other routes run their final theory checker. A conflict yields a
  blocking theory clause and another solve; absence of conflict permits the
  single-theory SAT result, with typed model construction when requested.
- **Dedicated LRA:** {source}`cb_check_found_model
  <src/smt/solver_engines.cpp>` validates observed-atom completeness, finishes
  any deferred tableau check, and rejects a conflict. Eager registration
  normally finishes this repair earlier through the external-clause callback.
- **Component recipes:** the model callback's `Partial` check is not the
  final combination decision. The outer loop first handles
  {source}`pending lemmas and typed failures
  <src/smt/component_solver.cpp#L307-L351>`. New lemmas are clausified at a
  detached atom-growth boundary before another solve. The
  {source}`coordinator <src/smt/component_model_coordinator.cpp#L140-L208>`
  then requests `FullModel` checks and verifies the selected arrangement/
  model-projection contract. Local consistency of each theory alone is
  insufficient.
- **Other logics:** {source}`QF_UFLRA <src/smt/solver_engines.cpp#L10956-L10982>`
  retains a dedicated combination driver. Integer search adds integrality and
  resource-limit obligations; quantified UF adds instantiation/completion
  obligations. Neither is explained by the ground-EUF example below.

Reversible model-equality choices are resubmitted as assumptions. An UNSAT
answer under those choices can reject only that branch:
{source}`the component loop <src/smt/component_solver.cpp#L352-L369>` learns
a clause over failed choices and retries, rather than immediately declaring
the input UNSAT. The SAT chapter explains ordinary assertion-scope selectors.

Cancellation, unsupported capabilities, unknown results, and resource exhaustion
must retain their typed outcomes. In particular, a component failure is not
an empty conflict clause. {source}`SolveTerminator
<src/smt/solver_runtime.cpp#L12-L27>` handles cancellation and its configured
theory-conflict budget; the [statistics lead](../leads/statistics-instrumentation.md)
records why this accounting cannot simply be disabled.

For requested models, theory facts and numeric values feed model construction
and reverse conversion. The {source}`session's finalization
<src/smt/solver_session.cpp#L649-L676>` checks original assertions in debug
builds or with `--check-model`, then prints the typed interpretation. This
check is conditional, and does not replace the solving obligations above or
independent validation. A model-construction failure is an internal error,
not evidence of theory UNSAT.

### Protected model scopes

The optional mixed online-EUF route borrows its live closure for tentative
model merges. During that scope the coordinator freezes the shared view and
queues all assignment, level, and rollback events. It replays them together
for every consumer after the scope ends. Ordinary callbacks cannot run while
protected; stored reasons may be streamed, but EUF cannot reconstruct a missing
reason from temporary merges. Before accepting a model, any queued notifications
force release, replay, and another SAT/theory round. The
{source}`coordinator tests <tests/unit/assignment_coordinator_tests.cpp>` cover
this scheduling protocol directly; mixed solver regressions cover model merging
and reconstructed-model validity.

## Worked example: equality rejects a Boolean branch

The following {download}`input <../examples/dpllt-equality.smt2>` has three answers:
`sat`, `unsat`, `sat`.

```{literalinclude} ../examples/dpllt-equality.smt2
:language: lisp
```

Write `A` for `a = b`, `B` for `b = c`, and `C` for `f(a) = f(c)`.
The assertion clauses are:

```text
¬p ∨ A
¬p ∨ B
¬p ∨ ¬C
```

Consider a branch choosing `p=true`. Boolean propagation gives `A`, `B`, and
`¬C`. EUF merges `a` with `b` and `b` with `c`; congruence consequently forces
`f(a) = f(c)`, contradicting `¬C`. Its explanation is the theory-valid clause
`¬A ∨ ¬B ∨ C`. In the dedicated online route, assignment notifications feed
the closure, {source}`maybe_store_conflict
<src/smt/solver_engines.cpp#L2554-L2565>` queues the explanation, and the
external-clause callbacks stream it to CaDiCaL.

One possible conflict-analysis derivation is:

```text
¬A ∨ ¬B ∨ C     resolve with ¬p ∨ ¬C on C  →  ¬p ∨ ¬A ∨ ¬B
¬p ∨ ¬A ∨ ¬B   resolve with ¬p ∨ B  on B  →  ¬p ∨ ¬A
¬p ∨ ¬A        resolve with ¬p ∨ A  on A  →  ¬p
```

The theory explanation and the learned clause `¬p` differ. The first expresses
an equality law; the second also depends on the input clauses. If `A` and `B`
arrive while `C` is unassigned, the same theory lemma can instead explain
propagating `C`. CaDiCaL can then encounter a Boolean conflict. These are two
schedules for the same logical obstruction.

With `p=false`, all three assertions hold, so the initial query is SAT.
Asserting `p` in the new scope makes it UNSAT; popping that assertion restores
SAT. The derivation illustrates a valid branch and resolution sequence, not
a promise about CaDiCaL's decision order, exact learned clause, or whether
preprocessing discovers the contradiction first. The example is checked across
lazy, online, conflict-only, and retained-session configurations in the
{ref}`review procedure <dpllt-chapter-checks>`.

## Sources and implementation status

This section is the implementation checklist for the integration described
above. It separates the papers' contracts from Primo's scheduling choices.

- **Nieuwenhuis, Oliveras, and Tinelli, *Solving SAT and SAT Modulo Theories:
  From an Abstract Davis–Putnam–Logemann–Loveland Procedure to DPLL(T)*,
  JACM 53(6), 937–977 (2006).**
  [Author text](https://homepage.cs.uiowa.edu/~tinelli/papers/NieOT-JACM-06.pdf),
  §§4.1, 4.4, 5.1–5.2. Basis: trail notifications/undo, theory-valid conflicts,
  optional non-exhaustive propagation, and explanations suitable for conflict
  analysis. These mechanisms are implemented through the owners above.
  Primo does not implement the paper's full abstract calculus or inherit its
  termination theorem merely by using callbacks. Explanation minimization and
  exhaustive propagation are not promised; kernel complexity is separate.
- **Fazekas, Niemetz, Preiner, Kirchweger, Szeider, and Biere,
  *IPASIR-UP: User Propagators for CDCL*, SAT 2023, §§2.1–2.4.**
  [Primary text and publication record](https://doi.org/10.4230/LIPIcs.SAT.2023.8).
  Basis: observation, trail notifications, delayed reason requests, model
  checks, and streamed external clauses. Primo implements this subset through
  its adapter, with the API differences listed in the [SAT chapter](sat.md).
  Proof-producing integration is not implemented. The installed dependency's
  clause-retention flags go beyond the paper's basic interface description.
- **Original Primo design:** {source}`component combination contract
  <docs/n-way-combination-migration.md>` and
  {source}`retained UF session design <docs/assumption-incremental-uf.md>`.
  These specify local ownership, detached growth, result adaptation, and
  session reuse. SAT backtracking, a callback's incremental theory state,
  and SMT-LIB `push`/`pop` are distinct lifetimes. The default session rebuilds;
  opt-in assumptions engines retain clauses and selectors across queries.

| Obligation | Existing verification and limitation |
| --- | --- |
| A theory conflict reaches SAT even without optional propagation | {source}`EUF external-conflict tests <tests/unit/boolean_solver_tests.cpp#L2589-L2635>`; ordinary solver regressions, not a proof of the kernel. |
| Explained propagation occurs during search | {source}`online congruence test <tests/unit/boolean_solver_tests.cpp#L2480-L2500>` checks delivered propagation; broader explanation algorithms belong in the EUF chapter. |
| Rollback preserves surviving reasons and assignment routing | {source}`component lifecycle regression <tests/unit/component_combination_tests.cpp#L526-L579>` and {source}`backend reconnect regressions <tests/unit/sat_backend_tests.cpp#L193-L235>`. |
| A candidate with pending work or a typed failure is not accepted as SMT SAT | Component-loop checks above and {source}`component regression suite <tests/unit/component_combination_tests.cpp>`; detailed combination/quantifier completeness is separate work. |
| Scoped assertions change satisfiability without corrupting earlier state | The worked example, {source}`session regressions <tests/unit/smt_session_tests.cpp>`, and the distinct session ownership described above. |

These checks support the documentation, not a new default promotion or a
performance claim. The [correctness gates](../development/correctness.md) still
apply to future changes to callbacks, explanations, scheduling, or final checks.
