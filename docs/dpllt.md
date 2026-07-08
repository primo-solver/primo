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
