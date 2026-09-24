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
backend. Online callbacks maintain assignment trails and decision levels,
produce explained conflicts/propagations, and undo state on backtracking.
Propagation options affect the schedule; they do not remove mandatory final
consistency or combination checks. The diagnostic lazy EUF engine remains
available with `--euf-engine=lazy`.

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
{source}`ComponentDispatcher <src/smt/component_dispatcher.cpp#L247-L255>`
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
