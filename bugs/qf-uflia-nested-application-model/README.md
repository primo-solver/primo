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

## Root cause, established 2026-09-06

**The model printer invents Int values for terms it has no value for, and
cannot tell a constrained term from an unconstrained one.**

The chain, in three steps:

1. `qf_uflia_int_values` (`src/smt/solver_engines.cpp`) returns no value for
   the nested applications. Its `combine` case for `Application` reads the
   term's EUF class value, and failing that matches the term against
   `valued_applications_by_symbol` -- applications whose class *already*
   carries a tableau value. With three levels of nesting there is no seeded
   application to match against, so every level returns `nullopt`.
2. `ModelBuilder::term_value` (`src/smt/model_builder.cpp`) treats a missing
   value as ordinary. Its third fallback calls `fresh_int_value`, which hands
   out `0, 1, 2, ...` skipping values already in use.
3. That invented number is used both as the **table key** for the application's
   arguments and, one level up, as an argument **result**. The row it produces
   contradicts the constraint that pinned the term.

Instrumented on `witness.smt2`, three values are invented -- and all three are
`f` applications:

```
[INVENTED] rep=9  kind=Application value=2
[INVENTED] rep=8  kind=Application value=4
[INVENTED] rep=25 kind=Application value=5
```

which is exactly the printed table's `(ite (= x0 0) 5 (ite (= x0 3) 1 (ite (= x0 4) 2 0)))`.

This explains both conditions the section above records as necessary, and they
turn out to be the two halves of the same requirement -- something must be
invented, and something must contradict it:

| case | invented | model |
|---|---:|---|
| one level of nesting | 0 | valid |
| two levels | 0 | valid |
| **three levels** (`witness.smt2`) | **3** | **invalid** |
| three levels without `(<= v1 6)` | 4 | valid |
| **`witness-simplification-sensitive.smt2`** | **5** | **invalid** |
| the same with `--no-extended-simplification` | 2 | valid |

Inventing is *necessary but not sufficient*: with nothing pinning the term, an
arbitrary value is a perfectly good one, which is why the unpinned case invents
four values and still validates.

**Step 2 is fixed, 2026-09-06.** `reconcile_invented_applications` in
`src/smt/model_builder.cpp` walks the term DAG bottom-up on an explicit stack
and keys the completion by the *argument value tuple* rather than by
congruence class -- which is what the printed table is keyed by, and what the
invention was not. Applications carrying a solver value are settled in a first
sweep so their rows exist before any invention can occupy the same key. The
term DAG is acyclic, so no fixpoint or seeding is involved; the candidate
matching in `qf_uflia_int_values`, whose comment describes a cycle, creates that
cycle by searching sibling applications rather than descending.

`witness.smt2` and `witness-generated.smt2` now validate. The completion is
self-consistent by construction: every application's printed value is the row
its own arguments select.

**A second, independent defect remains, and it is the one that is not easy.**
`witness-backward-constraint.smt2` is the seven-line reduction:

```smt2
(set-logic QF_UFLIA)
(declare-fun z () Int)
(declare-fun y () Int)
(declare-fun f (Int) Int)
(declare-fun g (Int Int) Int)
(assert (= z (g (g (f 2) (f y)) (+ (f 2) (f y)))))
(assert (<= z (- 1)))
(check-sat)
```

Primo prints `z = 4` against `(<= z (- 1))`. The printed `g` table carries
`g(6,0) = -1` -- the solver's own row, with the value `z` should have -- but the
chain evaluates the inner `g` to `3`, so the outer application lands on a
different row. The solver's value for the *inner* application never reached the
builder, so it was invented, and the invention cannot know that `z`'s bound
pins it to `6`.

That direction is backward: `z`'s bound constrains the outer row, which
constrains the inner argument. Bottom-up evaluation cannot recover it, because
it is constraint solving rather than evaluation. The fix belongs in step 1 --
`qf_uflia_int_values` has to return the value the solver actually assigned that
application -- and not in the printer, which can only be consistent with the
values it is given.

`:model.invented-int-values` now reports the count, so the situation is visible
from `--stats` alone. `invented_int_model_values_are_counted` in
`tests/unit/qf_uflia_solver_tests.cpp` pins the two-versus-three-level boundary.

## Still not established

- Whether the same shape can produce a wrong `sat` where no model exists at
  all. Every witness so far is genuinely satisfiable; only the printed model
  is wrong.
- Why `qf_uflia_int_values` cannot seed the chain at three levels when it can
  at two. Step 1 above says what it does, not why the purified proxy for the
  innermost application carries no tableau value that congruence could
  propagate outwards. `witness-backward-constraint.smt2` is the smallest case
  where that matters, because there the missing value is not free: an
  arithmetic bound decides it.

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

Reproduced again after `ite` became a term kind: 5 model errors among 110 sat
cases on a 250-case `--shape composed` run, about 4.5%. One of the five,
`case-00149`, contains no `ite`, `div`, `mod` or `abs` at all -- only nested
`(f (f (g ...)))` with a pinned result -- which is how that run was confirmed
to be reproducing this bug rather than a regression from the new term kinds.

Dolmen itself was initially unavailable on this workstation: `opam` refused
because its depext probe wanted the system packages `libgmp-dev` and
`pkg-config` and the sandbox has no `sudo`. `pkg-config` is in fact installed
and GMP comes from `contrib/local`, so `contrib/install_dolmen.py` now passes
`--no-depexts` and the install succeeds without root.

## Second witness, 2026-09-06: `witness-simplification-sensitive.smt2`

Found by the QF_UFLIA model fuzz gate (job 132640) on the branch that made
`ite` a term kind. 21 of 3000 cases produced an invalid model; **20 of the 21
are invalid on the pre-change binary `7ea99a7` too**, and this is the one that
is not.

It is the same defect, not a new one. The failing assertion is

```smt2
(assert (= z (g (g (f 2) (f y)) (+ (f 2) (f y)))))
```

with the printed model `f = 0`, `z = 5` and

```smt2
(define-fun g ((x0 Int) (x1 Int)) Int
  (ite (and (= x0 0) (= x1 0)) 8
  (ite (and (= x0 7) (= x1 0)) 5
  (ite (and (= x0 8) (= x1 0)) (- 1) 0))))
```

`f 2` and `f y` are both `0`, so the inner `g` is `g(0,0) = 8` and the outer is
`g(8,0) = -1`, against `z = 5`. The table also carries `g(7,0) = 5`, so the
solver was working with `7` for an inner application its own table evaluates to
`8` -- the self-inconsistent table on nested applications this bug is about.

**What it adds: a reproduction lever.** The result depends on the
extended-term simplification, and not on the preprocessing mode:

| configuration | model |
|---|---|
| default | invalid |
| `--preprocessing-mode=basic` / `strong` / `off` | invalid |
| `--no-ite-equality-distribution` | invalid |
| `--no-extended-simplification` | **valid** |

The leaf collapses `(ite (< (f 2) (f y)) (f 2) (f 2))` to `(f 2)` -- both
branches are the same term -- which is correct, and changes which model the
builder produces. That is the same "makes the shape reachable more often"
effect this file already records for the `2706164` worklist rewrite, arriving
from a different direction. Base is afflicted independently: it reproduces the
original `witness.smt2` above.

So a fix should be checked against both witnesses, and the
`--no-extended-simplification` split is a cheap way to get the same input to
take two different model-building paths.
