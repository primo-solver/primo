# DPLL(T) integration

Primo combines SAT search with theory checks both during callbacks and after a
complete Boolean assignment. This is implemented behavior, not a future step
beyond a lazy prototype. The SAT backend remains CaDiCaL; Primo does not contain
a fallback SAT solver. See [](sat-ipasir-up.md) for callback contracts and
[](current-status.md) for the route matrix.

The [clause-forgetting note](papers/clause-forgetting.md) distinguishes the
original theory-conflict explanation from the clause learned by conflict
analysis. Any future forgetting policy needs both live-reason safety and a
progress argument covering the backend and outer SMT loop. This is an open
investigation; the note does not change current clause retention.

## Control flow and ownership

### Shared assignment protocol: implementation underway

The shared-stack migration replaces independently mutable Boolean assignment
copies with one SMT-owned coordinator per SAT connection. The storage types
`sat::Trail` and `sat::TrailView` contain only literal IDs: an ordered trail,
variable-to-position index, decision boundaries, and an epoch. Truth is read
from the indexed trail entry rather than stored in another value array.
Consumers borrow a read-only view and receive accepted assignment deltas and
the exact removed literals on backtracking. Term registries and theory undo
records remain separate. This section is the local implementation checklist;
route migration and qualification are tracked in [](active-work.md).

This is a projection of the SAT assignment onto observed variables, not a
second complete SAT trail. Tseytin variables used only for Boolean structure
remain inside CaDiCaL. A compound Boolean function argument is different:
`f(and p q)` is rewritten to `f(b)` with a fresh Boolean term `b` and the SAT
definition `b ↔ (p ∧ q)`. The default registration mode observes the variable
for `b`; EUF uses its assignment to merge `b` with `true` or `false`, without
introducing a separate SAT equality variable. The older optional
`bridge-atoms` mode instead adds explicit equality atoms and linking clauses;
see [](euf-boolean-arguments.md). Decision boundaries include levels with no
observed assignments. The dense variable-to-position index can still contain
empty slots for unobserved variables below its largest observed ID.

The extra assignment storage is required by our current consumer design and
notification interface, not by DPLL(T) itself. Eliminating it would require a
backend API exposing equivalent assignment/order/rollback information with
explicit lifetime rules; the current migration does not add that API.

All dedicated, retained-session, mixed, component, and quantified-UF routes now
use this boundary. `AssignmentConnection` disconnects the backend while the
coordinator and consumers still live, then unbinds borrowed views. The
component driver's detached preparation boundary also uses this lifetime guard.
`ComponentAssignmentView` owns only atom-registration metadata and derives
values from the common trail; quantifiers construct a temporary projection in
SAT assignment order. `TheoryCombinationManager` reads interface assignments
from the same view, while retaining its distinct model-equality undo log.
`IncrementalFlatAssignmentState` keeps closure checkpoints, not an assignment
copy. Lazy routes populate one completed-model snapshot without enabling
online propagation. Mixed routes observe new interface atoms before every
SAT round, including when only their arithmetic child is online.

Production migration is implemented; final fuzzing and baseline comparison
are still pending. The earlier rollback qualification does not qualify this
new ownership change.

The basis is Fazekas et al., *IPASIR-UP: User Propagators for CDCL*, SAT 2023,
[Section 2.2](https://doi.org/10.4230/LIPIcs.SAT.2023.8), read together with the
installed CaDiCaL vector-notification contract. This is an original ownership
design implementing that contract, not a new solving algorithm.

- Each decision records the current trail length, even without an observed
  assignment. Boundary `k` begins level `k+1`; backtrack to `k` retains its
  prefix. Root assignments survive; same-level rollback is a no-op.
- Identical assignment notifications are idempotent. Contradictory assignments
  without rollback, invalid literals, and invalid levels are internal errors.
- Append and lookup are amortized constant time; rollback visits only removed
  literals. Registration growth never invalidates the view's owner pointer.
  No borrowed entry/range may survive a mutating callback.
- The coordinator commits state before dispatching the corresponding delta.
  Nested notifications are queued in order. Protected model scopes defer the
  entire notification stream, not only EUF, until borrowed model views are
  released. Ordinary solving/checking callbacks cannot run while protected.
- Reasons for retained propagations survive unrelated backtracking. Removal
  events prune only discarded reasons; immutable stored reason streams remain
  available while model scopes are protected.
- Online final-model callbacks validate the notified state; they do not repair
  lost markers. Offline checks load a completed-model snapshot through the
  same view interface. Disconnect finishes reason handling before owner
  destruction; reconnect initializes a fresh view and replays permanent facts.
- Deterministic tests cover retained/root facts, empty levels, duplicate and
  invalid events, reentrancy, protection, growth, reason lifetime and reconnect.
  Differential fuzzing and both normal/ASan suites qualify each migrated route.

`solver_session.cpp` owns the frontend, original assertion roots, preprocessing
and result adaptation. Tactic goals own conversion records. `solver_strategy.cpp`
selects the leaf after preprocessing; `solver_leaves.cpp` adapts it to the engine.
`solver_engines.cpp` contains the pure-theory and dedicated QF_UFLRA engines;
the QF_UFLIA recipe enters `solve_component_recipe` in `component_solver.cpp`.
Public `check_sat.cpp` is only the session boundary wrapper.

The unused empty `smt::Dpllt` class and its include-only translation unit were
removed during the documentation rewrite. They had no call sites and owned
no solver state. The drivers listed above remain separate; this cleanup does
not harmonize their scheduling or change solver behavior. The replacement
chapter is `docs-next/implementation/dpllt.md`, with a runnable equality example
and links to the actual owners.

Boolean abstraction belongs to `BooleanClausifier`. Theory components receive
typed literals and explanations, never SMT-LIB syntax or ownership of the SAT
backend. `AssignmentCoordinator` maintains the shared observed assignment and
decision boundaries. Consumers read `sat::TrailView`, produce explained
conflicts/propagations, and undo their kernel state on backtracking.
Propagation options affect the schedule; they do not remove mandatory final
consistency or combination checks. The diagnostic lazy EUF engine remains
available with `--euf-engine=lazy`.

The [EUF callback regression](euf.md#callback-assignment-rollback) records the
repair of a decision-boundary indexing mismatch that lost propagations after
backtracking. The shared assignment owner now replaces the duplicated callback bookkeeping;
its cross-route qualification is tracked above.

The following is the final-check skeleton, not the complete callback protocol:

```text
loop:
  result = sat_backend.solve()

  if interrupted, resource-limited, or result == unknown:
      return the typed stop result

  if result == unsat:
      return unsat

  assignment = sat_backend.model()
  theory_literals = abstraction.true_theory_literals(assignment)

  for each theory solver:
      theory_solver.synchronize_or_rebuild()
      theory_solver.assert_relevant_literals(theory_literals)

  if all theory solvers are consistent:
      reconcile their shared arrangement when combination is required
      if a reversible model choice is needed, solve under assumptions and repeat
      if agreement is established, construct the requested model and return sat

  conflict = some_theory_solver.conflict_clause()
  sat_backend.add_clause(conflict)
```

Local consistency alone is insufficient for a mixed SAT answer. QF_UFLIA's
collection coordinator validates shared projections and retains complete
arrangements when bounded model guidance is exhausted. QF_UFLRA uses its
specialized convex reconciliation and explained exact fallback. Model-equal
pairs are choices, not entailed facts; failed assumptions justify blocking
clauses. See [](n-way-combination-migration.md) and [](nelson-oppen.md).

When requested, final component facts and numeric values feed the typed model
adapter, followed by reverse conversion replay. Debug builds or `--check-model`
check the original assertions; serialization occurs only after the interpretation
is frozen. Structural/model errors are internal failures, not theory UNSAT.
See [](model-interpretation.md).

Incremental theory execution is solve-scoped. It does not implement SMT-LIB
`push`/`pop`, nor the full paper `DPLL(T^M)` transition system. Cancellation is
carried by per-invocation callbacks and an RAII SAT terminator, not a global
active solver pointer.

Do not add a fallback in-house SAT solver. If CaDiCaL/IPASIR-UP is missing, report the missing dependency clearly.

## Sources and implementation status

The dedicated EUF/LRA callbacks request non-forgettable external conflicts in
the API variant with that flag, whereas
{source}`ComponentDispatcher <src/smt/component_dispatcher.cpp#L240-L248>`
marks its external clauses forgettable. Reason-vector lifetime is separate
from this eligibility flag and from actual backend deletion. The
[forgetting note](papers/clause-forgetting.md) records this route distinction.

The SAT callback contract is mapped to the IPASIR-UP paper in
[](papers/ipasir-up.md). The implemented theory kernels and their subsets are
documented in [](euf.md) and [](arithmetic.md); mixed arrangement and model-choice
obligations are in [](nelson-oppen.md) and [](n-way-combination-migration.md).
The skeleton above is an integration overview, not a new algorithm or a claim
to implement the full transition systems in those papers.

## CaDiCaL Variable Ownership

Primo is an incremental SMT client of CaDiCaL: it may call `solve()`, then add
new theory lemmas, blocking clauses, or fresh abstraction variables, and call
`solve()` again. With CaDiCaL 3.0, bounded variable addition (`factor`) is
enabled by default and can introduce internal extension variables. CaDiCaL's
API contract therefore requires incremental clients to obtain fresh user
variables with `declare_more_variables()` when that API is available; using
`resize()` after `factor` has introduced extension variables can make a later
Primo clause reuse an internal variable number and CaDiCaL will abort.

`sat::CadicalBackend::new_var()` owns this boundary. It prefers
`declare_more_variables(1)` and falls back to `resize()`/`reserve()` only for
older CaDiCaL versions that do not expose the declaration API. Solver layers
must not create SAT variable numbers directly.
