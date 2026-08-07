# OpenSMT returns `sat` for an unsatisfiable QF_UFLRA formula

## Summary

OpenSMT 2.9.2 returns `sat` for the following QF_UFLRA formula. The formula is
unsatisfiable because the Boolean clauses imply `a = b`, while the explicit
constraint `f(a) != f(b)` contradicts uninterpreted-function congruence, which
requires `f(a) = f(b)`.

The independent Real constraint is intentionally present. It is satisfiable
on its own and does not cause the contradiction; it forces the input through
the mixed QF_UFLRA/Nelson-Oppen path.

## Reproducer

```smt2
(set-logic QF_UFLRA)
(declare-fun a () Bool)
(declare-fun b () Bool)
(declare-fun f (Bool) Bool)
(declare-fun x () Real)

; These two clauses imply a = b.
(assert (or (not a) b))
(assert (or (not b) a))

; EUF congruence then implies f(a) = f(b), so this is contradictory.
(assert (not (= (f a) (f b))))

; Independent satisfiable Real constraint. Its purpose is to select QF_UFLRA.
(assert (<= 0 x))
(check-sat)
```

## Observed results

| Solver | Result |
|---|---|
| OpenSMT 2.9.2 | `sat` |
| Z3 | `unsat` |
| cvc5 1.3.4 | `unsat` |
| Yices 2.7.0 | `unsat` |

## Control experiment

Removing the Real declaration, the Real assertion, and changing the logic to
`QF_UF` makes OpenSMT return `unsat`. This suggests that the issue is specific
to the mixed-theory path rather than pure Boolean EUF.

## Reproduction command

```bash
opensmt reproducer.smt2
```
