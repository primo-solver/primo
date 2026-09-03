# QF_UFLIA invalid model: nested Int applications get an inconsistent UF table

## Status

**OPEN.** Found 2026-09-04 while validating models after the
`qf_uflia_int_values` worklist rewrite (`2706164`). It is **not** caused by
that rewrite: the binary at `2706164^` produces the byte-identical invalid
model. The rewrite only made the shape reachable more often, because the
recursive evaluator used to crash on nested applications instead of answering.

This is an invalid model, not a wrong sat/unsat answer: the reported status is
correct, but the printed UF table does not satisfy the input.

## Reproduction

`witness.smt2`, five lines:

```smt2
(set-logic QF_UFLIA)
(declare-fun f (Int) Int)
(declare-fun v1 () Int)
(assert (<= v1 6))
(assert (= v1 (f (f (+ (f 3) (- 1))))))
(check-sat)
```

With `--produce-models`, Primo answers `sat` with

```smt2
(define-fun v1 () Int 2)
(define-fun f ((x0 Int)) Int (ite (= x0 0) 5 (ite (= x0 3) 1 (ite (= x0 4) 2 0))))
```

Evaluating the second assertion under that table gives
`f(f(f(3) + (-1))) = f(f(1 - 1)) = f(f(0)) = f(5) = 0`, not `2`. Feeding the
model plus the assertions to Z3 returns `unsat`; see `outputs.txt`.

The table is self-inconsistent rather than merely incomplete. Reading it
backwards, the solver's internal model must have used `f(3) = 5`, because
`5 - 1 = 4` and `f(4) = 2 = v1` closes the chain. The printed table instead
carries `f(3) = 1` and puts `5` at key `0`, so one application's argument was
keyed by a different value than the one used to compute its result.

## What is known

- Three nesting levels are required. `witness.smt2` with one `f` removed
  (`(= v1 (f (+ (f 3) (- 1))))`) produces a valid model.
- A constraint that pins `v1` is required. Dropping `(<= v1 6)` produces a
  valid model.
- Frequency: 3 of 60 randomly generated QF_UFLIA cases in the generator shape
  used here (nested `f`/`h` applications over `+` with small integer
  constants). Identical failure set before and after `2706164`.
- `witness-generated.smt2` is the unreduced generated case (`case43`).
- The two sibling generated failures reduce to the same shape.

## Not yet established

- Whether the fault is in `qf_uflia_int_values` argument keying, in the EUF
  class value assignment it reads, or in the model printer that turns those
  into `ite` chains.
- Whether the same shape can produce a wrong `sat` where no model exists at
  all. Every witness so far is genuinely satisfiable; only the printed model
  is wrong.

## Detection gap

`make tactic-composition-test` and the QF_UFLIA fuzz gates validate models with
Dolmen. Dolmen could not be installed on this workstation -- `opam` needs the
system packages `libgmp-dev` and `pkg-config`, and the sandbox has no `sudo` --
so local model validation had been skipped. These witnesses came from a direct
Z3 re-check instead. The SLURM gates do run Dolmen and should be expected to
find this shape once the generator reaches it.
