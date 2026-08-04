# Lazy DPLL(T)

The first DPLL(T) loop may be naive and lazy.

```text
loop:
  result = sat_backend.solve()

  if result == unsat:
      return unsat

  assignment = sat_backend.model()
  theory_literals = abstraction.true_theory_literals(assignment)

  for each theory solver:
      theory_solver.reset_or_rebuild()
      theory_solver.assert_relevant_literals(theory_literals)

  if all theory solvers are consistent:
      return sat

  conflict = some_theory_solver.conflict_clause()
  sat_backend.add_clause(conflict)
```

The first implementation may rebuild theory state from the current SAT assignment on each check. Later milestones may add incremental theory state, propagation, and backtracking.

Do not add a fallback in-house SAT solver. If CaDiCaL/IPASIR-UP is missing, report the missing dependency clearly.

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
