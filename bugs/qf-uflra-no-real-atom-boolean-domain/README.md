# QF_UFLRA wrong answer when no Real atom survives preprocessing

## Status

**Open Primo soundness bug.** Primo answers `sat` on an unsatisfiable
QF_UFLRA formula. z3 5.0.0 and cvc5 1.3.4 both answer `unsat`. Unlike
[](../opensmt-qf-uflra-boolean-disagreement/README.md), which is a
reference-solver discrepancy, this one is ours.

## Reproduction

```bash
./build/primo --quiet bugs/qf-uflra-no-real-atom-boolean-domain/witness.smt2   # sat
contrib/local/bin/z3   bugs/qf-uflra-no-real-atom-boolean-domain/witness.smt2  # unsat
contrib/local/bin/cvc5 bugs/qf-uflra-no-real-atom-boolean-domain/witness.smt2  # unsat
```

## Why the formula is unsat

`(distinct (f false) (f true))` makes `f` injective on the two-element `Bool`
domain, so `(= (f p) (f q))` forces `p = q`. Then `(g p q)` is `(g q q)`,
contradicting `(not (g q q))`.

Deciding that requires knowing that `p` and `q`, as `Bool` arguments of an
uninterpreted function, range over exactly `{true, false}` and are congruent
with the `true`/`false` constants. Primo does that with a preprocessing step
whose counters are `:no.boolean-domain.bridged-terms` and
`:no.boolean-domain.equality-atoms`.

## What triggers it

**The formula has no Real atom.** `x` is declared and never constrained, so
the logic is QF_UFLRA but nothing reaches the arithmetic theory. On that path
the Boolean-domain bridging does not run: with a Real atom present the two
counters read 8 and 17, and without one **neither counter is emitted at all**.

The existing regression test
`boolean_solver_tests.cpp:bool_argument_congruence_preserves_boolean_domain`
is the same formula plus `(assert (< 0 x))`, and it passes. The Real atom in
it is load-bearing in a way the test does not say -- it is what keeps the
input on the mixed path. The same observation is already recorded, for the
opposite reason, in `../opensmt-qf-uflra-boolean-disagreement/github-issue.md`:
"The independent Real constraint is intentionally present ... it forces the
input through the mixed QF_UFLRA/Nelson-Oppen path."

## How it was found

Promoting `--monotone-elimination` to default (2026-08-26). That pass
discharges one-sidedly-occurring Real variables, and on the test formula it
eliminates `x` and folds `(< 0 x)` away -- leaving exactly the shape above,
which flipped the test from `unsat` to `sat`. The pass is not at fault: the
bug reproduces with the flag off once the Real atom is deleted by hand, and
therefore exists on main today. What promotion changed is how easy it is to
reach: any QF_UFLRA input whose only Real atoms are one-sided becomes a
candidate.

**Promotion is blocked on this**, and the default was reverted rather than
shipped alongside a live wrong answer. See `docs/run-log.json`'s
`2026-08-26-lra-model-phase-monotone-promotion-test` for the corpus evidence
that motivated the promotion, which is otherwise sound.

## Fix sketch, not yet attempted

Either run Boolean-domain bridging whenever the logic declares uninterpreted
functions over `Bool`, independent of whether any Real atom survives, or route
an arithmetic-free QF_UFLRA instance to the QF_UF path that already does the
bridging. Which of the two is right depends on where the dispatch decision is
made, which has not been traced yet. A regression test should cover both
shapes -- with and without a Real atom -- since the existing one only covers
the shape that works.
