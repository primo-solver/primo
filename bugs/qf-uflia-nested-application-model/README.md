# QF_UFLIA invalid model: nested Int applications get an inconsistent UF table

## Status

**LARGELY FIXED 2026-09-06, and not closed.** The QF_UFLIA model fuzz gate went
from **21 model errors in 3000 cases to 0** on seed base 202609100 (job
132988), and all four witnesses in this directory validate. But a run on a
*different* seed base the next day (202609200, job 133726) produced **1 model
error in 3000**, so the class is reduced by roughly 95% rather than eliminated,
and "0 in 3000" was a statement about one seed rather than about the bug.

The survivor, `witness-residual-2026-09-07.smt2`, is the same shape as
everything else here -- nested applications `(f (f (ite ...)))` and
`(f (+ (f z) (g y w)))` -- and it fails identically with and without
`--cite-simplification`, so it is not caused by the CITE work that found it. It
reports 12 invented Int values against 5 purification labels, so the unvalued
terms are again ones no label covers.

Two commits, and the second is the one that finishes it:

- `e2058f1` reads a UF application's value off its **purification label**
  instead of searching for it, which is the construction the literature
  describes (see the section below). 21 errors to 2.
- `76e5e37` fixes what was left: reading *both* substitution kinds, applying
  substitutions beyond the printed terms, recording the argument-proxy
  definitions that reach LIA as bounds and never become EUF atoms, and -- the
  actual remaining bug -- retrying evaluation after a value arrives, because
  `evaluate` memoizes failures and arena order can reach a substitution before
  the constant it needs. 2 to 0.

The history below is kept because two plausible fixes were tried and measured
first, and one of them made things 75% worse.

Originally found 2026-09-04

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

**A consistent-completion fix was tried on 2026-09-06 and refuted.** Recording
it because it looks obviously right, fixes two of the three witnesses, and
still makes the corpus worse.

The idea: the printed table is keyed by the *argument value tuple* while
`fresh_int_value` memoizes by *congruence class*, so key the completion by the
argument tuple instead. Walk the term DAG bottom-up -- it is acyclic, so every
application is settled after its arguments -- and let each application take, or
establish, the row its own arguments select. No fixpoint and no seeding is
needed; the cycle that `qf_uflia_int_values`' comment describes comes from its
own strategy of searching sibling applications rather than descending.

It does what it says. `witness.smt2` and `witness-generated.smt2` both
validate: witness 1 goes from a self-contradictory `f(3)=1, f(0)=5, f(4)=2`
with `v1=2` to `f(3)=1, f(0)=2, f(2)=4` with `v1=4`, which evaluates correctly
and satisfies `(<= v1 6)`.

**On the corpus it is a regression.** Same 750 generated cases, same seed,
one binary differing only in this pass:

| model builder | model errors |
|---|---:|
| unchanged | **8** |
| bottom-up completion, single pass | 14 |
| bottom-up completion, solver-valued applications in a first sweep | (worse still; 43 against 21 over 3000 on the gate) |

The two-sweep variant is separately wrong -- in the first sweep an argument
with no solver value is not yet settled, so `term_value` invents for it and the
rows that sweep publishes are keyed on values the second sweep then changes --
but fixing that ordering still leaves 14 against 8.

**Why it backfires.** Forcing an application onto an existing row equates two
applications whenever their *printed* argument values coincide. Those values
are partly invented, so a collision between an invented argument value and a
real one merges two applications the solver deliberately kept apart, and the
merge propagates upwards. The old per-class invention avoided this by accident:
distinct classes got distinct invented values, so collisions were rarer. Keying
by argument tuple is right for *consistency* and wrong for *containment*, and
the completion is not correct until invented argument values are guaranteed not
to collide with values that carry meaning.

So the printer can be made self-consistent, and self-consistency is not
sufficient. A real fix has to stop the values going missing in the first place
-- see the backward-constraint witness below, where the missing value is not
even free.

## Third witness: `witness-backward-constraint.smt2`

Seven lines, and the smallest case where the missing value is not free:

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
`g(6,0) = -1` -- the solver's own row, with the value `z` should have -- but
the chain evaluates the inner `g` to `3`, so the outer application lands on a
different row. The solver's value for the *inner* application never reached the
builder.

The constraint that decides it runs backward: `z`'s bound pins the outer row,
which pins the inner argument to `6`. Bottom-up evaluation cannot recover that,
because it is constraint solving rather than evaluation, which is the other
half of why the completion fix above is not enough. The value has to come from
`qf_uflia_int_values`.

## What the literature says, and where Primo departs from it

This is a solved problem in the standard presentation, and the answer was in
the repository's own paper collection the whole time.

[](../../docs/papers/griggio-mathsat-thesis.md) sect. 1.5.1 (`docs/papers/griggio.pdf`,
p. 30) gives the construction. Purification rewrites a mixed formula by
labelling every alien subterm with a fresh variable and asserting the label:

```
f(x + 3y) = g(2x - y)
  ==>  f(v1) = g(v2)  /\  v1 = x + 3y  /\  v2 = 2x - y
```

The labels are **interface variables**: they occur in both pure parts, so the
arithmetic solver assigns each of them a value, and Nelson-Oppen's requirement
is precisely that the two models agree on the arrangement -- the equivalence
relation over those shared variables. The combined model is then read off
rather than searched for. Every UF application in the *purified* formula has
the form `f(v_1, ..., v_n)` with a label of its own, so its row is
`(val(v_1), ..., val(v_n)) -> val(v_f)`, and **no value can be missing**. That
is the property Primo does not have.

**Primo purifies and then discards the map.** The counters confirm the
purification happens and is exactly the right size: `witness.smt2` reports
`:no.purification.arithmetic-uf-definitions 3`, one per `f` application, and
`witness-backward-constraint.smt2` reports 4. But
`MixedTheoryPurificationResult` (`src/smt/solver_engines.cpp`) returns only the
rewritten assertions and a set of *counts* -- there is no map from a label back
to the application it labelled.

So `qf_uflia_int_values` has to rediscover the correspondence, which it attempts
in two ways, both indirect: rebuilding congruence classes from the asserted
equality literals, and merging any two tableau terms that happen to share a
value. Instrumented on `witness.smt2`:

```
tableau_terms=4  int_apps=6  apps_with_class_value=3  asserted_eqs=4
```

Six Int applications for a formula with three `f` applications -- the arena
holds both the originals and the purified copies -- and only three carry a
value. `ModelBuilder::table_entries` then iterates over *all* application terms
in the arena, so one printed table mixes rows from both representations, keyed
by values that only half of them have. The other half is invented, which is
where this bug starts.

**What this implies for a fix.** Not a better search. The label-to-application
map already exists at purification time and is thrown away; retaining it makes
the completion a lookup, and makes the backward-constraint witness a non-issue,
because the inner application's value is its label's tableau value rather than
something to be inferred from an arithmetic bound. It also explains why the
consistent-completion attempt above could not work: it reconciled the printed
table with itself while still drawing on the same half-populated value map, and
it had no way to tell an original application from its purified copy.

## The systematic angle, 2026-09-07

Five instances now, each a different channel that model construction failed to
consult: the purification map, one of the two substitution kinds, a memoised
failure never retried, a compound value invented independently of its children,
and -- still open -- an Int equality owned by LIA that never reaches the model
builder's congruence closure.

**The pattern is the diagnosis.** There is no single source of truth. Values
are scavenged from the LIA tableau, EUF classes, purification labels, two
substitution kinds and value-coincidence merging, with silent invention as the
fallback. Every elimination pass adds a new way for a value to go missing, and
nothing enforces an invariant, so each fix consults one more channel and the
next instance arrives from a channel nobody thought of.

**One piece is now structural rather than scavenged.** Interpreted arithmetic
-- `Plus`, `Times`, `Ite` -- is always computed from its children and never
looked up, so a compound can no longer contradict them. Invention is confined
to uninterpreted leaves, which is where it is legitimate: a constant or an
application at an argument tuple the solver never pinned.

**A model self-check was tried and is not landed.** The idea is to evaluate the
assertions under the model before printing it, turning a wrong model into an
immediate local failure instead of something an external validator finds later.
It works in the sense that matters -- it flags the open witness -- but it
reported **48 of 211 models broken on a corpus Dolmen passes**, and a
diagnostic with a 23% false-alarm rate is worse than none.

Three rounds of narrowing got it from 63 to 48 false alarms and the remaining
ones share a cause worth recording, because it is the same cause as the bug:
**there is no single evaluation function.** The builder computes values one way
(`int_value` plus the congruence closure), printing goes through the table's
deduplicated rows, and a checker is a third path. Any disagreement among the
three shows up as a false alarm, and every operator has to be made to agree
separately -- arithmetic, ite, three-valued propagation for terms the checker
cannot decide, applications through rows rather than through internal values.

So the self-check is not a small addition on top of the current design; it is
the same refactor the bug needs. The real fix is one evaluator that both fills
the table and answers queries, so that printing and checking cannot disagree
by construction. That is the shape of the systematic fix, and it is bigger
than any of the five patches.

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

## The residue, found and fixed 2026-09-06

Both surviving cases are fixed. Three separate things had to be true for
`case-00130` to print a wrong model, and the last of them is the interesting
one.

### 1. `x` had no value, and the reason was not the purification map

The map was working: `pairs=8 seeded=8 proxy_unvalued=0`. The failing assertion
is

```smt2
(assert (= x (f (+ (ite (>= z 0) y (- 2)) (f y)))))
```

and every term in the chain had the solver's value -- `(f y) = 16`, the
purified argument `= 14`, `(f 14) = -1` -- except `x`, which had none and was
invented as `1`.

`x` is in none of the EUF atoms (all eight are purification definitions, and
`x = f(...)` is an Int equality that goes to LIA), and it is not an LIA variable
either: the tableau's nine variables are *all* internal `$primo_` symbols. A
user constant reaches the model only through substitution facts.

### 2. Only one kind of substitution was being read

`x` is substituted to term 40, `f($primo_ite_0 + f(y))`, so its value is that
term's. That term's value needs `$primo_ite_0`, which had itself been
substituted to the literal `-2` -- through a
`DirectedEqualitySubstitutionConversion`, while the value pass collected only
`LiaSubstitutionConversion`. Both kinds eliminate a constant equally
thoroughly, so both are now read.

Substitutions were also being applied to `model_terms` only, four terms here.
They are now applied to every substituted Int term, which is what the
neighbouring comment about "every evaluable Int term" already intended.

### 3. The memoized failure, which is what actually kept it broken

With both of those fixed, `$primo_ite_0` was valued at `-2` and `(f y)` at
`16`, and their sum was *still* unvalued. `evaluate` memoizes failures as well
as successes -- that is what keeps it linear -- and the terms are visited in
arena order. `x` is term 22 and `$primo_ite_0` is term 33, so `x`'s
substitution is evaluated first, descends into the sum, finds term 33 unvalued,
and caches that failure permanently. The value arrives eleven terms later and
nothing ever asks again.

The pass now runs in rounds, discarding only the *failed* entries between
rounds and stopping as soon as a round settles nothing new. Successes are kept,
so no subgraph is walked twice.

### Result

| | model errors |
|---|---:|
| before the purification map | 21 / 3000 |
| purification map | 2 / 3000 |
| **with the residue fixed** | **0 in the 750-case local corpus; both survivors valid** |

`:model.invented-int-values` falls from 8 to 6 on `case-00130` and 5 to 4 on
`case-00454`. It is not zero: some applications in the arena are genuinely
unconstrained and inventing for those is correct. All four witnesses in this
directory validate, `make asan-test` and `make smoke` pass, and the 27 unit
tests pass.

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
