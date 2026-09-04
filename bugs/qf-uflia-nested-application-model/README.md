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

## Confirmation with the project validator

```
$ python3 scripts/validate_model.py --solver ./build/primo --require-dolmen-pass \
    bugs/qf-uflia-nested-application-model/witness.smt2
witness.smt2: model-error answer=sat validator=z3-fallback
```

## Why the fuzz gates did not find it

Two gates covered QF_UFLIA and the bug sat in the seam between them.

`scripts/qf_uflia_fuzz.py` drew every atom from a fixed vocabulary of 24
hand-written strings and never composed terms, so no Int application was ever
nested inside another: measured over a 1200-case corpus, the maximum UF
application nesting was exactly 1 in all 1200 cases. That gate is the one that
runs model validation.

`scripts/slurm_qf_uflia_mutation_fuzz_gate.sbatch` mutates real MathSAT seeds
and therefore has realistic term structure, but it never asked Primo for a
model -- `produce-model` appeared nowhere in it or in `scripts/yinyang_fuzz.sh`,
which run the solver as `primo --quiet`. The `model_validate=true` in that gate
is a Z3 option that makes Z3 check its own model.

So: structural coverage without models, model coverage without structure. The
generator and `qf_uflia_int_values` were also added in the same commit
(`e82bae6`), so the generator encodes the same picture of the input space as
the code it exercises.

Both halves are now fixed. `qf_uflia_fuzz.py` gained a `composed` shape that
builds terms bottom-up to a `--depth`, and the mutation gate retains its
mutants and runs them through `scripts/validate_model.py`. On a 300-case
`--shape composed` run the new generator reaches nesting depth 3 in most cases,
204 of 300 contain a directly self-nested `(f (f ...))`, and Dolmen reports
**8 model errors among 141 sat cases** -- this bug, at roughly 6%, where the old
generator found none in 1200.

The full gate confirms it. SLURM job 131459 (4 shards, 750 cases each, the new
default `mixed` shape) reports **18 model errors among 1509 validated sat
models**, every one on a case whose UF application nesting is 2 or 3. The same
gate at the same commit with the old `--shape random` (job 131454) reports zero
across 1809 validated models.

Dolmen itself was initially unavailable on this workstation: `opam` refused
because its depext probe wanted the system packages `libgmp-dev` and
`pkg-config` and the sandbox has no `sudo`. `pkg-config` is in fact installed
and GMP comes from `contrib/local`, so `contrib/install_dolmen.py` now passes
`--no-depexts` and the install succeeds without root.
