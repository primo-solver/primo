# Incremental UF model construction over original ITE arguments

Status: fixed locally; fresh exact-commit SLURM qualification pending.

Fresh gate 142579, seed 202639843, found a second-query model error at d96902f.
The same error reproduces on the preserved 1595948 binary with Auto and Off;
rebuild answers the full generated session correctly. The insertion-scan fix is
therefore unrelated to this failure.

The minimized witness asserts `f(ite b a c)`. The engine built its model from the
active original roots together with the lowered theory assignment. The original
application and `f(aux)` were distinct closure nodes, yet evaluated at the same
argument tuple. Boolean completion invented a value for the original application
which disagreed with the assigned lowered predicate.

`IncrementalUfEngine` now owns active lowered model roots and push boundaries.
It constructs from those roots plus retained theory assignments. Session model
checking still evaluates the original assertions; no check is removed. Pop
truncates active model roots while retaining historical SAT atoms and terms.
The generated witness and a minimized scoped witness are retained here. The
unit test covers Auto/Off, repeat checks, a contradictory scope, pop/reappend,
empty-scope root counts and immutable returned models.

Reproduce with `primo --quiet --check-model --max-theory-conflicts=10000
--incremental-mode=assumptions < witness.smt2` (and `--preprocessing-mode=off`).
The generated witness also has independent query-context Z3 comparisons and
actual session-model Dolmen validation through `scripts/validate_incremental.py`.
See `docs/assumption-incremental-uf.md` for the original repair checklist.
