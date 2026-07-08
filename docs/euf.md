# EUF and congruence closure

The first theory target is QF_UF.

## Interface sketch

```text
EufSolver:
  push()
  pop(n)
  assert_eq(t1, t2, source_lit)
  assert_neq(t1, t2, source_lit)
  check() -> sat | conflict
  conflict_clause() -> list[SatLit]
  are_equal(t1, t2) -> bool
  explain_eq(t1, t2) -> list[TheoryLit]
```

## Milestones

- EUF0: union-find over `TermId`
- EUF1: register application terms
- EUF2: merge equality classes
- EUF3: maintain congruence closure
- EUF4: support disequality constraints
- EUF5: detect equality/disequality conflicts
- EUF6: produce conflict clauses
- EUF7: produce explanations
- EUF8: produce models

See `docs/papers/fast-congruence-closure.md`.
