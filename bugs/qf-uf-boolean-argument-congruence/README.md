# QF_UF wrong answer: Boolean UF arguments are not tied to true/false

## Status

**FIXED 2026-08-26 in `fa44295`.** Kept as the reproduction record; the
witnesses are regression seeds under `benchmarks/seeds/qf_uf/` and unit tests
in `tests/unit/boolean_solver_tests.cpp`.

The fix is registration, not new clauses: `register_boolean_argument_terms()`
registers Bool-sorted *arguments* of uninterpreted applications as theory
atoms, and the branches described under "Root cause" -- which already existed
in both EUF engines -- then assert `t = true/false` with the term's own literal
as the reason. It costs no SAT variable and no clause. Both solver paths now go
through one `apply_boolean_domain_encoding` dispatcher so they cannot drift
apart again, which is what produced this bug.

Verified: the Bool harness went 91 disagreements in 600 cases to 0, the SLURM
gate ran 8,000 cases across all eight shapes clean, the QF_UF division showed
**zero answer changes** against the pre-fix binary, and the QF_UFLRA A/B showed
zero disagreements between the two encodings. See `docs/run-log.json`'s
`2026-08-26-qf-uf-boolean-cardinality-fix`.

## Reproduction

The smallest case has two assertions and no Bool-valued function at all:

```smt2
(set-logic QF_UF)
(declare-sort U 0)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun tb (Bool) U)
(assert (= (tb true) (tb false)))       ; tb is constant on Bool
(assert (not (= (tb p) (tb q))))        ; so this cannot hold
(check-sat)
```

`tb` maps both elements of `Bool` to the same `U` value, so `tb(p) = tb(q)`
for every `p` and `q`. Primo answers `sat`; z3 and cvc5 answer `unsat`. It is
`benchmarks/seeds/qf_uf/seed_bool_argument_domain_unsat.smt2`.

```bash
for f in benchmarks/seeds/qf_uf/seed_bool_*_unsat.smt2; do
  echo "$f $(./build/primo --quiet "$f") $(contrib/local/bin/z3 "$f")"
done
python3 scripts/qf_uf_boolean_fuzz.py --count 600 --shape all   # 91/600 disagree
```

The four-assertion witness below is the case the promotion originally
surfaced; it is kept because it exercises the same gap through congruence on a
second function rather than through a constant image.

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

The QF_UF path is *not* broadly broken. Everything whose contradiction is
reachable without reasoning about `Bool` being two-valued is answered
correctly, because SAT plus ordinary congruence closure suffices.
`scripts/qf_uf_boolean_fuzz.py`, 600 cases over all shapes, 2026-08-26:

| shape | cases | disagreements |
| --- | ---: | ---: |
| result (Bool-valued applications as formulas) | 80 | **0** |
| nested (applications feeding applications) | 81 | **0** |
| compound (`and`/`or`/`ite` in argument position) | 86 | **0** |
| argument | 88 | 12 |
| mixed-sort (`Bool -> U`, `U -> Bool` chains) | 92 | 13 |
| two-valued (needs `|Bool| = 2`) | 90 | 1 |
| converse (argument equality derivable only from injectivity) | 83 | 65 |

Every disagreement reduces to the same missing fact -- that a Bool-sorted
*argument* term equals one of the two constants. Forward congruence, the
Bool-valued result link, and the injectivity shape when the argument
disequality is stated propositionally are all correct.

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
