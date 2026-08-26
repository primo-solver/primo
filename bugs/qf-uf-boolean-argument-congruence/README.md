# QF_UF wrong answer: Boolean UF arguments are not tied to true/false

## Status

**Open Primo soundness bug.** Primo answers `sat` on an unsatisfiable QF_UF
formula, and the model it prints does not satisfy that formula. z3 5.0.0 and
cvc5 1.3.4 both answer `unsat`.

Scope is the **pure QF_UF solving path** (`solve_qf_uf`). That path also
handles QF_UFLRA inputs with no surviving Real atom, so `witness-uflra.smt2`
is wrong for the same reason. The mixed QF_UFLRA path is unaffected.

## Reproduction

```bash
./build/primo --quiet bugs/qf-uf-boolean-argument-congruence/witness.smt2         # sat  (wrong)
./build/primo --quiet bugs/qf-uf-boolean-argument-congruence/witness-uflra.smt2   # sat  (wrong)
contrib/local/bin/z3   bugs/qf-uf-boolean-argument-congruence/witness.smt2        # unsat
contrib/local/bin/cvc5 bugs/qf-uf-boolean-argument-congruence/witness.smt2        # unsat
```

## The formula is unsat

`(distinct (f false) (f true))` makes `f` injective on the two-element `Bool`
domain. With `(= (f p) (f q))` that forces `p = q`. Then `(g p q)` is `(g q q)`,
contradicting `(not (g q q))`.

## Primo's model refutes Primo

Asking for the model makes the bug self-evident -- no reference solver needed:

```text
(define-fun p () Bool false)
(define-fun q () Bool false)
(define-fun f ((x0 Bool)) Bool (ite (= x0 false) true (ite (= x0 true) true false)))
(define-fun g ((x0 Bool) (x1 Bool)) Bool (ite (and (= x0 false) (= x1 false)) true false))
```

`f false` and `f true` are both `true`, so the printed model violates
`(distinct (f false) (f true))`. It also sets `g false false` to `true` while
`q` is `false`, violating `(not (g q q))`.

The third `ite` branch in `f` is the tell: a `Bool -> Bool` function needs two
cases, and the model printer emitted three. The EUF solver found its model in a
universe where the `Bool` sort has more than two elements.

## Root cause

`p` and `q` occur as **arguments of uninterpreted functions**, so they are EUF
terms as well as SAT variables. Deciding this formula needs the step
`f(p) = f(q)` **implies** `p = q`, which is not congruence -- it is the
converse, and it holds only because `Bool` has exactly two elements and
`(distinct (f false) (f true))` makes `f` injective on them. To derive it, the
solver has to case-split on the argument terms, which requires the EUF terms
`p` and `q` to be linked to the EUF constants `true` and `false`.

`BooleanClausifier::materialize_boolean_argument_literals()` (check_sat.cpp)
is that link: it creates the `(= term true)` / `(= term false)` atoms, selects
them from the term's SAT literal, enforces mutual exclusion, and asserts the
two constants distinct. Counters `:no.boolean-domain.bridged-terms` and
`:no.boolean-domain.equality-atoms`.

**It is called from exactly one place: `solve_qf_uf_lra`.** `solve_qf_uf` never
calls it.

## What still works, and why that hid the bug

The QF_UF path is *not* broadly broken, which is why this went unnoticed. Every
shape whose contradiction is reachable without that case split is answered
correctly, because SAT plus ordinary congruence closure is enough:

| shape | QF_UF result |
| --- | --- |
| `(= p q)` asserted, `(distinct (f p) (f q))` -- plain congruence | correct |
| `(= p q)` asserted, contradiction through a Bool-valued result `(g p q)` | correct |
| `f(p) = f(q)`, `f` injective, and `p != q` stated as `xor`, as two clauses, or as `(not (= p q))` | correct |
| three pairwise-distinct Bool-valued applications | correct (decided propositionally) |
| **`f(p) = f(q)`, `f` injective, and `p != q` only derivable** | **WRONG** |

Six of the eight `benchmarks/seeds/qf_uflra/seed_bool_*` seeds, converted to
QF_UF by hand, are answered correctly. The failing case is specifically the one
where the argument equality must be *derived* from two-valuedness rather than
being given.

## Why the test suite did not catch it

`boolean_solver_tests.cpp:bool_argument_congruence_preserves_boolean_domain`
is this exact formula plus `(assert (< 0 x))`, and it passes. That Real atom is
load-bearing in a way the test never states: it is what routes the input to the
mixed path. Every regression case for this semantics was written in QF_UFLRA.
`docs/euf-boolean-arguments.md` describes the bridge as a property of "mixed
solving" throughout, and commit `47fd382` (2026-08-07, "Fix Boolean argument
congruence in mixed EUF") fixed the mixed path only. The pure QF_UF path was
never given the same treatment, and nothing tested that it had been.

A simpler shape escapes by accident: with a unit like `(assert (not p))`,
preprocessing substitutes the constant for `p` before solving, so the formula
becomes syntactically contradictory and never reaches EUF. That is why the bug
needs `p` and `q` to be unit-free to show up, and why small hand-written checks
of "does congruence work on Bool arguments" can pass while the bug is present.

## How it was found

Promoting `--monotone-elimination` to default (2026-08-26). The pass discharges
one-sided Real variables; on the regression test above it eliminated `x`,
folded `(< 0 x)` away, and moved the test off the mixed path -- flipping it from
`unsat` to `sat`. The pass is not at fault. Deleting that atom by hand
reproduces the wrong answer with the flag off, and the QF_UF form of the
witness needs no flag and no arithmetic at all.

Promotion is blocked on this: it would make the bug reachable for any QF_UFLRA
input whose only Real atoms are one-sided. See `docs/run-log.json`'s
`2026-08-26-lra-model-phase-monotone-promotion-test`.

## Fix sketch, not yet attempted

Call `materialize_boolean_argument_literals()` from `solve_qf_uf` as well, and
check whether the QF_UF EUF setup needs the canonical `true`/`false` constants
created the same way the mixed path creates them. Regression tests must cover
**both** shapes -- with and without arithmetic -- because the existing suite
only covers the one that works, and a QF_UF-only case is the one that was
missing.
