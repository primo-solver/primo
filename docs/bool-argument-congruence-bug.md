# Wrong answer: congruence is dropped for Bool-sorted function arguments

**Severity: wrong answer.** Found 2026-08-07 while checking a proposed change
to the theory dispatch. This is not a performance issue and not the
Nelson-Oppen regression in [](nelson-oppen-qf-lra-regression.md); it is
long-standing and independent.

## Reproducer

```lisp
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(assert (= p q))
(assert (not (= (f p) (f q))))
(check-sat)
```

Congruence gives `p = q` implies `f(p) = f(q)`, so this is **unsat**.

| solver | answer |
|---|---|
| z3 | `unsat` |
| Primo `0c0843d` (main) | **`sat`** |
| Primo `b6b8d55` (pre-NO-merge) | **`sat`** |

Deterministic across repeated runs. Present before and after the
Nelson-Oppen merge, so it is not a regression from that work.

## What it is not

- **Not an EUF bug.** Congruence over Bool arguments works correctly when the
  EUF solver actually runs. Adding an uninterpreted sort to the same formula,
  which forces the QF_UF path, gives the right answer:

  ```lisp
  (declare-sort S 0) (declare-fun z () S) (declare-fun g (S) S)
  (assert (= (g z) z))          ; forces :solver.qf-uf
  (assert (= p q))
  (assert (not (= (f p) (f q))))
  ```
  → Primo `unsat`, matching z3.

- **Not out of scope.** [](architecture.md) states that Bool arguments to
  uninterpreted functions are supported: *"SMT-LIB `Bool` is a first-class
  sort, so QF_UF inputs may pass Boolean formulas as arguments to
  uninterpreted functions."*

- **Not caused or fixed by the proposed dispatch narrowing** in
  [](nelson-oppen-qf-lra-regression.md). That change touches
  `has_euf_predicate`, which -- see below -- is not consulted on this path at
  all.

## Cause: two behaviours compose

**1. Bool-sorted equalities are rewritten before the classifier runs.**
`summarize_formula_features` computes `has_equality` from `Equality` nodes, but
preprocessing rewrites Bool-sorted equalities into Boolean structure first. So
for a formula whose equalities are *all* over Bool -- `(= p q)` and
`(= (f p) (f q))` here -- **`has_equality` is false**.

**2. The QF_UF dispatch keys only on `has_equality`.**

```cpp
if (has_equality) {
  return finish_solve(solve_qf_uf(...));
}
try {
  return finish_solve(solve_boolean_conjunction(...));   // no congruence
} catch (const UnsupportedInput&) { ... solve_qf_uf ... }
```

`has_euf_predicate` is computed and used for the *mixed* EUF/LRA dispatch, but
it is **not consulted here**. So a formula containing a genuine uninterpreted
function is handed to `solve_boolean_conjunction`, which abstracts `(f p)` and
`(f q)` to independent propositional variables and has no congruence rule to
relate them. `p = q` becomes an iff between two SAT literals, which does not
constrain the two abstracted applications at all -- hence `sat`.

Note the direction of the failure: the classifier is **too narrow** here,
which is exactly the unsoundness direction flagged in
[](nelson-oppen-qf-lra-regression.md)'s fix-ordering table. It is already
happening, before any change.

## Why the fuzzers did not catch it

Neither gate generates this shape. FuzzSMT is driven by its parameters and the
QF_UF/QF_LRA invocations do not request Bool-sorted function arguments;
mutation fuzzing (opfuzz/typefuzz) can only produce what its seed corpus
contains, and the QF_LRA seeds have no uninterpreted functions at all. So a
documented-as-supported feature has no differential coverage. See
[](fuzz-coverage-qf-lra.md) -- the same "a fuzzer covers only what it is told
to cover" failure that note already records for seed families, in a new place.

## Fix direction, and the trap in it

The dispatch must send a formula containing uninterpreted function
applications to `solve_qf_uf` regardless of whether any surviving `Equality`
node remains. `has_application_term` is already computed and is the more
robust test than `has_euf_predicate`, since it does not depend on the
application's sort.

**The trap:** this widens what reaches `solve_qf_uf`, so formulas that are
currently answered by the Boolean solver would move. That is the *sound*
direction, but it is still a dispatch change, with the same
benchmark-comparability hazard: files change solver, not speed. Measure
gained/lost, not PAR2 alone.

**Falsifier:** the reproducer above must answer `unsat`, and the
uninterpreted-sort variant must keep answering `unsat`.

**Regression tests to add:**

1. The reproducer -- `unsat`.
2. `f : Bool -> Bool` with `p`, `q` *not* asserted equal -- `sat`, so the fix
   is not simply forcing unsat.
3. A Bool-argument congruence chain through two applications.
4. A purely propositional formula with no uninterpreted functions -- must
   still reach `solve_boolean_conjunction`, so the widening is not total.
5. `(f (and p q))` -- a *compound* Boolean argument, which
   [](architecture.md) says the frontend normalizes with a fresh constant and
   a propositional equivalence; that path needs its own coverage.
