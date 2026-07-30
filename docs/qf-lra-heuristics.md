# QF_LRA Speedup Heuristics: Design Note

Written 2026-07-27 as a design note; **H2, H1a, H1b and H3 are implemented
and promoted to default; H7 is implemented but NOT promoted (opt-in, see its
falsifier outcome); H4, H5 and H6 remain designs** (each item below carries
its own status). It picks up where
the competitiveness roadmap's items 0-8 (see [](arithmetic.md)'s Milestone
roadmap) left off, and is grounded in a fresh local
profiling/instrumentation pass run at commit `35878c4` -- not in the older
3-file evidence items 0-6 were designed against.

**Where things stand on the 237-file promotion bar** (10s timeout, z3 167 and
cvc5 150 unchanged throughout):

| | solved / 237 | PAR2 | gap to z3 |
|---|---|---|---|
| before this note (job 114200) | 108 (45.6%) | 2675.6s | 59 files |
| + H2 (job 114900) | 124 (52.3%) | 2380.9s | 43 files |
| + H1a (job 115172) | 136 (57.4%) | 2149.8s | 31 files |
| + H1b (job 117222, `bland`) | 144 (60.8%) | 1962.4s | 23 files |
| + H3 (job 117222, new default) | **168 (70.9%)** | **1538.9s** | **z3 is 167** |

Zero wrong answers throughout, and zero regressed files at every step. **Primo
now solves one file more than z3 (168 vs 167) with PAR2 essentially tied
(1538.9s vs 1531.6s)**, and the two are complementary rather than one
dominating: 6 files z3 solves and Primo does not, 7 the other way round.
cvc5 is at 150.

Sections 1.1 and 1.2 below describe the state *before* any of this; Section
2's per-item status blocks describe what changed.

The roadmap's own closing lesson is taken as binding here: **the 9-file
"dramatic-gap" sample is for *finding* techniques, the 237-file stratified
sample (`benchmarks/qf-lra-stratified-sample.txt`) is the promotion bar.**
Every item below therefore states which families it targets, why, and what
would falsify it.

## 1. Where the remaining gap actually is

From the last full 237-file comparison (Slurm job 114200, 10s timeout;
primo 108, z3 167, cvc5 150; zero wrong answers), **61 files are solved by
z3 or cvc5 but not by Primo**. Grouped by family, with the fastest
reference time:

| family | gap files | fastest ref | note |
|---|---|---|---|
| TM | 9 | 0.18s | Boolean-guarded arithmetic chains |
| uart | 9 | 0.19s | ditto |
| sc | 8 | 0.19s | min/max comparator chains |
| 2019-ezsmt | 7 | 0.33s | LP/blending + labyrinth |
| latendresse | 6 | 0.54s | pure conjunctive LP (flux balance) |
| miplib | 5 | 0.57s | LP relaxations |
| tta_startup | 5 | 0.13s | near-difference constraints |
| DTP-Scheduling | 4 | 0.46s | **pure** difference constraints |
| clock_synchro | 4 | 0.49s | near-difference constraints |
| Heizmann / LassoRanker | 3 | 2.17s | hard for everyone |

Families where Primo already matches both references (keymaera,
meti-tarski, sal, spider_benchmarks, check) and families where all three
solvers fail (tropical-matrix) are excluded from the design targets below.

### 1.1 Three distinct bottleneck classes, not one

Fresh `--stats` runs (local, Release `build/primo`, commit `35878c4`)
separate the gap files into three classes with *different* dominant costs:

| instance | answer | time | decisions | conflicts | pivots | check calls |
|---|---|---|---|---|---|---|
| `latendresse/ecoliFBAyicesTest-500` | (timeout) | >30s | **0** | 0 | <2000 | **1** |
| `DTP-Scheduling/constraints-tempo-width-30` | sat | 19.5s | 60,750 | **0** | 7,182 | 63,021 |
| `clock_synchro/clocksynchro_2clocks.main_invar.induct` | unsat | 13.9s | 1,515 | 116 | 8,997 | 1,072 |
| `miplib/pp08a-12000` | sat | 13.9s | 4,564 | 70 | 8,314 | 910 |
| `tta_startup/simple_startup_4nodes.bug.induct` | sat | 7.1s | 35,878 | 2,893 | 46,490 | 19,287 |

- **Class A -- pure LP, one `check()` call.** `latendresse` has *zero*
  decisions: the formula is one big conjunction, so the entire runtime is a
  single Simplex solve that never finishes. Nothing about SAT integration,
  propagation, or decision heuristics can matter here. (Note the irony:
  these are the very flux-balance benchmarks the SOI paper was written for,
  and `--lra-pivoting-rule soi` times out on them too.)
- **Class B -- pivot-cost dominated.** `clock_synchro` and `miplib` need
  only 1.5k-4.5k decisions but spend ~1.5 ms *per pivot*.
- **Class C -- assignment-volume dominated.** `DTP-Scheduling` makes 60,750
  decisions and **zero conflicts** -- the SAT solver never learns anything,
  it just walks to a model -- yet takes 19.5s, i.e. ~320 µs per decision.

### 1.2 Profile: the cost is allocation, not arithmetic

`perf record -g` on `constraints-tempo-width-30` (Class C, the *least*
pivot-heavy of the three classes), flat self-time:

```
31.4%  LraTableau::pivot
42.0%  allocator  (_int_malloc 10.5, _int_free 7.9, malloc 5.8, cfree 5.2,
                   malloc_consolidate 5.2, unlink_chunk 4.3, gmp_default_allocate 1.5, ...)
12.0%  GMP copy/init (__gmpn_copyi 5.6, __gmpz_init_set 3.4, __gmpq_clear 1.6, ...)
 5.0%  GMP arithmetic (__gmpz_mul 1.4, __gmpq_mul 1.0, __gmpz_gcd 0.8,
                       __gmpn_gcd_1 0.7, __gmpz_divexact_gcd 0.5, __gmpq_aors 0.5)
 1.3%  LraTableau::rebuild_occurs_in_index
```

This **corrects a conclusion recorded during roadmap item 0's follow-up**,
which stated that what remained after the container fixes was "GMP's own --
`mpq_class`'s arithmetic operators canonicalize (GCD-reduce) on every
add/multiply by design, an inherent cost of exact rational arithmetic no
container-level fix can remove." Measured: canonicalization/GCD is under
2%, and *all* real bignum arithmetic is ~5%. Roughly **54% of runtime is
malloc/free and copying of tiny numbers**. The values are small; the
representation is expensive.

### 1.3 Instrumentation: tableau shape and fill-in

Temporary instrumentation (added, measured, reverted -- not committed)
reported the prepared tableau's shape and how row density evolves during
search.

Prepared tableau shape (one row per distinct polynomial, as built by
`PreparedLraTheory`):

| instance | rows | rows that are a **single variable** | share | avg row | max row |
|---|---|---|---|---|---|
| `latendresse/ecoliFBAyicesTest-500` | 4,555 | 3,161 | **69%** | 3.39 | 691 |
| `TM/p6-zenonumeric_s5` | 1,378 | 1,311 | **95%** | 1.97 | 50 |
| `miplib/pp08a-12000` | 400 | 262 | **66%** | 2.52 | 123 |
| `sc/sc-17.induction3` | 1,185 | 462 | 39% | 1.82 | 8 |
| `uart/uart-17.induction` | 1,446 | 526 | 36% | 1.75 | 3 |
| `tta_startup/simple_startup_4nodes.bug.induct` | 584 | 84 | 14% | 2.01 | 4 |
| `clock_synchro/clocksynchro_2clocks` | 264 | 5 | 2% | 2.75 | 7 |
| `DTP-Scheduling/constraints-tempo-width-30` | 3,932 | 0 | 0% | **2.00** | **2** |
| `LassoRanker/selectSort...phaseTemplate` | 9,469 | 653 | 7% | 5.50 | 15 |

Fill-in during search (sampled every N pivots):

| instance | initial avg row | avg row during search | max row | max `occurs_in` |
|---|---|---|---|---|
| `DTP-Scheduling/constraints-tempo-width-30` | 2.00 | 3.7 - 8.9 | 33 | **2,127** |
| `clock_synchro/clocksynchro_2clocks` | 2.75 | 8.1 - 14.3 | 34 | 174 |
| `latendresse/ecoliFBAyicesTest-500` | 3.39 | 7.9 - 23.9 | 961 | 183 |

Two facts follow directly, and they drive most of this note:

1. **A third to 95% of all tableau rows are trivial rows of the form
   `s = c·x`** -- one slack variable and one row allocated per bare
   single-variable atom. They are pure overhead: rows to pivot on, entries
   in `occurs_in_`, candidates in the violated-variable scan, and targets
   for fill-in.
2. **Rows densify 3-5x during search**, and a single variable can occur in
   over 2,000 rows. Since `pivot()`'s cost is
   `|occurs_in(entering)| × |row(leaving)|` row-combinations of GMP
   rationals, this is exactly the 31%-self-time hot spot -- and *no* rule
   currently implemented (Bland, Dantzig, SOI) prices a pivot by its cost.
   All three price only for *progress*.

## 2. Proposed heuristics, in priority order

Priority = (measured share of the bottleneck) × (breadth of families hit) ÷
(implementation and correctness risk).

### H1 -- Direct bounds for single-variable atoms (+ scale-canonical polynomials)

**Status: both halves implemented, unconditional and promoted.** H1a: 124 ->
136 solved on the 237-file bar, PAR2 -9.7%, zero regressions. H1b: 136 -> 144,
PAR2 -8.7% (+10 solved, -2 regressed), measured as the `bland` column of item
H3's four-way run rather than a run of its own. H1b's local evidence had been
genuinely mixed -- clock_synchro 3.1x faster and uart-18 1.5x, but
DTP-Scheduling 0.74x *slower* despite 8.6x fewer decisions -- and that
regression did not generalize: on the broad sample DTP-Scheduling went 11 ->
12. A reminder of why the broad sample is the bar and a single instance is
not.** See [](arithmetic.md)'s
"Direct bounds for single-variable atoms". The predicted row reductions were
confirmed exactly, family by family, via a new `:lra.rows` statistic --
ecoliFBA -69%, TM/p6 -95%, pp08a -66%, sc-17 -39%, uart-17 -36%, tta_startup
-14%, clock_synchro -2%, DTP-Scheduling unchanged -- each matching Section
1.3's measured single-variable share for that instance. Paired local A/B on
top of H2: TM/p5 5.3x, sc-21 3.2x, uart-18 1.3x, miplib/pp08a 1.2x,
clock_synchro and DTP flat (as their row counts predict).

**Prior art**: Monniaux (CAV 2009) Section 2.1 states this as ordinary
practice -- see [](papers/lra-related-work.md).

**Targets:** latendresse, TM, miplib, sc, uart (36-95% of rows removed);
neutral elsewhere. **Class A and B, plus the whole TM/sc/uart cluster.**

**Problem.** `PreparedLraTheory` allocates a slack variable and a tableau
row for *every* distinct polynomial, including a bare `x`. So `(<= x 2)`
becomes `s = x; s ≤ 2` instead of just `x ≤ 2`. Worse, polynomial keys are
exact-content matches, so `(<= 0 r1)` (normalizing to `-r1 ≤ 0`) and
`(<= r1 1000)` produce **two different slacks for the same variable** --
the tableau cannot even see that `r1` has both a lower and an upper bound
without pivoting. In `ecoliFBAyicesTest-500` that is 3,161 of 4,555 rows.

This is also a deviation from the source algorithm: Dutertre & de Moura's
Section 2 introduces slack variables only for *compound* polynomials;
atoms over a single variable are asserted directly as bounds on it.

**Design.**

- Canonicalize each atom's normalized `LinearPolynomial` by dividing
  through by the coefficient of its lowest-indexed variable (equivalently,
  by its leading coefficient), flipping the relation when that coefficient
  is negative, and folding the scale into the atom's right-hand side.
  Record the resulting `(canonical polynomial → slack)` map as today.
- If the canonical polynomial has exactly one term, it is now exactly `x`:
  do **not** allocate a slack or a row. Store
  `PreparedAtom{var = x, relation, rhs}` and assert the bound directly on
  the structural variable.
- Rename/generalize `atoms_by_slack` to `atoms_by_var`: an index from
  "tableau variable a bound was just asserted on" to "atoms that could now
  be entailed". The existing same-slack propagation scan works unchanged
  over it, and now also relates atoms that previously lived on different
  slacks.
- Delete `single_variable_slack` / `variable_for_single_slack` entirely.
  Row-based propagation (`compute_implied_bound`) can then read a row
  term's bound *directly* from `LraTableau::upper_bound`/`lower_bound`
  with no indirection, which both simplifies it and strictly increases its
  reach -- including for terms that today have no single-variable wrapper
  atom at all.

**Bonus:** the canonicalization step is exactly the not-yet-scheduled
roadmap item 3 (scale-invariant polynomial matching). `x ≤ 5` and
`2x ≤ 10` share a bound; `x + y ≤ 5` and `2x + 2y ≤ 10` share a slack.
That was listed as a separate future item; it comes for free here because
it is the same canonicalization.

**Also fixes**, without extra work, the quirk item 7 documented and worked
around: "`single_variable_slack` only registers a bare variable's
*positive*-coefficient normalized form, so `>=` atoms never get an entry".

**Risk: low-to-moderate.** Bounds now land on structural variables, which
can become basic through pivoting -- but `assert_upper`/`assert_lower`
already handle a basic target (`update_violation_status` + SOI direction
branch), and Farkas conflict generation already reads each variable's own
bound reason regardless of whether it is a slack. `LraTableau` itself needs
**no change at all**; this is entirely a `check_sat.cpp`
`PreparedLraTheory`/`OnlineLraPropagator` change. The correctness-sensitive
part is the rhs rescaling and relation flip (`c·x ≤ k` is `x ≤ k/c` for
`c > 0` and `x ≥ k/c` for `c < 0`), plus the strict/non-strict handling in
`DeltaRational` -- all directly unit-testable.

**Falsifier:** if row counts drop as predicted but wall time does not
improve on latendresse/TM/miplib, then row *count* was never the cost and
H3/H2 are the whole story.

**Falsifier outcome: partially triggered, and informative.** Row counts fell
exactly as predicted everywhere. Wall time followed on TM (5.3x), sc (3.2x),
uart and miplib -- but **not on latendresse**, which lost 69% of its rows and
did not move at all. So for the Class A pure-LP instance the falsifier fired:
row count was never its cost. What remains there is a 1,394-row LP with a
691-term dense row and no Boolean structure whatsoever, which is precisely
H3's fill-in problem. That sharpens H3 from "helps Class B" to "is the only
remaining lever for Class A", and it is the concrete reason to do H3 next.

### H2 -- Small-rational fast path in `util::Rational`

**Status: implemented, unconditional, 2026-07-28.** See
[](arithmetic.md)'s "Two-tier rational representation" for the shipped design
and `docs/run-log.json` for the verification trail. Paired local A/B on the
profiled instances: **2.1x-5.2x faster** (best-of-3), with every search counter
bit-identical (a pure representation change). **On the 237-file promotion bar:
solved 108 -> 124, PAR2 -11%, 16 newly solved, zero regressions, zero wrong
answers** (Slurm job 114900) -- it clears the bar that item 8 failed. Clean at
merge-gate and campaign-gate fuzz tiers on both pivoting paths. Two design
points below were
revised during implementation and are corrected in place: the small form is
64-bit rather than 32-bit (an ablation, not a settled question), and demotion
is **required**, not forbidden -- see the canonical-form paragraph.

**Prior art**: this is Monniaux's own implementation choice (CAV 2009,
Section 4), found only after the profiling above independently reached the same
diagnosis -- see [](papers/lra-related-work.md).

**Targets:** everything. **The single largest measured lever (54%).**

**Problem.** `util::Rational` wraps `mpq_class`; every value is two
heap-allocated GMP integers, and `DeltaRational` holds two of them. Section
1.2 measures ~42% allocator + ~12% GMP copy/init against ~5% actual
arithmetic.

**Design.** Tagged small/big representation behind the existing (already
fully encapsulating) public API:

- Fast path: `std::int64_t num, den` with `den > 0` and `gcd(num,den) = 1`,
  stored inline. Knuth's two-gcd addition and cross-reduced multiplication
  (TAOCP Vol. 2, Sec. 4.5.1) in `__int128`; binary GCD on machine words.
  *As shipped*, no overflow checks on the intermediates are needed at all:
  canonical operands bound every product by `2^126` and every sum by
  `2^127`, so only the reduced result needs a range test.
- Slow path: promote to a heap `mpq_class` when the reduced result doesn't
  fit -- and **demote again as soon as a result does fit**. The original
  version of this note had that backwards ("stay promoted -- demotion is a
  correctness footgun"); it is the opposite. Demotion is what makes the
  representation a function of the *value*, which is precisely what
  `RationalHash` needs: without it the same number could be stored two ways
  and hash two ways, and `PolynomialKey` hash-consing would silently stop
  matching. Implemented as a canonical-form invariant (big form iff the
  value does not fit small) established at exactly two private factories and
  asserted by a debug/ASAN-only `check_invariant()`.
- `sign()`, `is_zero()`, comparison, and negation are branch-light and
  allocation-free on the fast path. With the invariant in place,
  `RationalHash` can hash the small pair directly and `operator==` can
  reject a form mismatch outright.
- No call-site changes: `mpq_class` already appears only inside
  `rational.hpp/.cpp`, exactly as that file's own comment intends.
- Width: 64-bit rather than Monniaux's 32-bit, behind the single `kSmallMax`
  knob so the two can be compared empirically. **Ablation run 2026-07-28 --
  64-bit kept.** On instances whose coefficients never leave 32 bits the two
  widths are within noise, or 32-bit is up to ~13% faster (`__int128`
  operations are multi-instruction); on the one instance that does overflow,
  64-bit is 2.2x faster because it cuts big-form operations 12-fold. A few
  percent on the common case for a factor of two on the hard one. The
  ablation also turned up a real latent bug -- see [](arithmetic.md).

**Risk: moderate, concentrated and testable.** It is the numeric core, so a
bug is a wrong answer rather than a slowdown. Mitigation as planned:
randomized differential unit tests against `mpq_class` for `+ - * /`, all
comparisons, `sign`, `is_zero`, `to_string`, and `parse`, with adversarial
magnitudes straddling the overflow boundary; ASAN/UBSan suite; and the full
campaign-tier fuzz gate on both pivoting paths (unconditional core code, so a
campaign-gate change by the standing precedent).

*This paid off during implementation*: the ASAN run caught a real disagreement
between `from_mpq`'s "fits small" test (`mpz_fits_slong_p`, which admits
`INT64_MIN`) and the small form's own range (which excludes `INT64_MIN` so
negation can never overflow) -- the two differed by exactly that one value.
Fixed by giving both a single shared predicate. A separate deliberate-mutation
check (dropping demotion) confirmed the unit tests fail loudly when the
invariant is broken, which matters because that particular bug is otherwise
silent.

**Expected:** removal of most of the ~54%; realistically 1.5-2x
solver-wide on QF_LRA, and it compounds with every other item here since
they all reduce *counts* of the same operations.

**Measured: 2.1x-5.2x** on the profiled instances -- better than expected,
because the allocator cost was compounding across `DeltaRational` (two
`Rational`s per bound and per assignment) rather than being paid once. The
weakest case (`clocksynchro_2clocks`, 2.1x) is the one instance whose
coefficients genuinely outgrow 64 bits: 1.22M promotions, so it keeps paying
GMP's cost. That is also the first hard evidence for which instances the
width ablation could still move.

### H3 -- Fill-in-aware (Markowitz-style) entering-variable selection

**Prior art, checked 2026-07-30 (after implementation).** This rule is the field's standard rather than a technique transferred from one paper. King's thesis (NYU 2014) Section 2.2.12 records minimum-column-length entering selection with a Bland fallback past a finite cap as what MathSAT, OpenSMT, Yices, Yices 2, z3 and CVC4 all do -- i.e. H3 and its anti-cycling story exactly. The Monniaux citation below remains an accurate source for the sparsity criterion, but H3 should not be read as novel. The *leaving*-side knob is the part with no counterpart there: King records Griggio's most-violated rule (CVC4, MathSAT5), which is our Dantzig choice, not our shortest-row one. See [](papers/lra-related-work.md).

**Status: implemented 2026-07-28, promoted to default 2026-07-29
(`LraPivotingRule::Sparse` with the leaving-side half on).** On the 237-file
bar: **168/237 against the former default's 144, PAR2 1538.9s vs 1962.4s,
+24 files with zero regressions**, campaign-gated on both pivoting paths.
See [](arithmetic.md) and `docs/run-log.json`. See [](arithmetic.md)'s "Fill-in-aware pivot
selection" for the shipped design. First local numbers, and one result that
was not anticipated:

| instance | bland | dantzig | sparse | sparse + leaving |
|---|---|---|---|---|
| `sc-21.induction3` | 1.56s | 3.43s | **0.72s** | -- |
| `clock_synchro/clocksynchro_2clocks` | 1.17s | 0.50s | **0.22s** | -- |
| `DTP-Scheduling/constraints-tempo-width-30` | 7.41s | 5.96s | **1.93s** | -- |
| `miplib/pp08a-12000` | 1.63s | **0.86s** | 1.82s | -- |
| `latendresse/ecoliFBAyicesTest-*` (6 files) | timeout | ~19.5s | timeout | **~0.65s** |

**The unanticipated result is the last row.** The leaving-side knob
(`--lra-sparse-leaving`, repair a short row first) was planned as a
lower-priority follow-up precisely because it is *not* progress-neutral. It
turns out to be what cracks Class A: every `latendresse` file goes from
"never solved by Primo" to ~0.65s, roughly 30x faster than the only other
rule that solves them at all, with `:lra.pivot-term-ops` 8.7M against
Dantzig's 130.9M. That family is 0/15 on the promotion bar today and z3 only
manages 6/15.

In hindsight it is the expected shape: a pure-LP instance is one `check()`
call with no Boolean structure, so "which infeasibility to repair" has no
search-trajectory consequences to get wrong -- the objection that made the
knob risky simply does not apply to Class A. It is still a real risk on the
Boolean-heavy classes, which is why it stays a separate flag and gets its own
column rather than being folded into `sparse`.

Second finding, also worth keeping: **Dantzig has never been run on the
237-file bar** in three roadmap cycles, and it solves `latendresse` where
Bland does not. Any sparse-vs-bland comparison that omits it would
misattribute part of the win, which is why the promotion run is four-way
(bland / dantzig / sparse / sparse+leaving).

**Targets:** Class B (clock_synchro, miplib) and Class A (latendresse)
directly; helps Class C too, since Section 1.2's profile is from a Class C
file and still shows `pivot()` first.

**Problem.** `pivot(leaving, entering)` rewrites every row in
`occurs_in_[entering]`, merging in the new entering row -- cost
≈ `|occurs_in(entering)| × |row(leaving)|` rational operations. Bland picks
the *smallest index* entering candidate; Dantzig picks the *largest
|coefficient|*; SOI picks by global objective decrease. **None of the three
considers what the pivot will cost or how much fill-in it creates**, and
Section 1.3 measures rows densifying 3-5x with `occurs_in` reaching 2,127.

In floating-point LP that trade-off is dominated by numerical stability, so
textbook pricing rules ignore fill-in and leave it to the LU
refactorization layer. Primo has *exact* rationals: there is no stability
constraint at all, and no refactorization layer -- so the fill-in term is
unopposed, and this is where Primo's cost structure genuinely differs from
the LP literature's assumptions.

**Design.** A new `PivotingRule::Sparse` (opt-in first, same rollout
precedent as Dantzig/SOI), layered on the existing selection loop:

- Leaving variable: as Dantzig (most-violated), from `violated_vars_`.
- Entering variable: among the eligible candidates in `row(leaving)`,
  minimize the Markowitz-style score `(|occurs_in(v)| - 1) × (|row(leaving)| - 1)`.
  Since `|row(leaving)|` is fixed once the leaving row is chosen, this
  reduces to **minimizing `|occurs_in(v)|`** -- an O(1) lookup per
  candidate, no arithmetic, so the rule is strictly cheaper to evaluate
  than Dantzig's coefficient comparison.
- Tie-break 1: prefer `|coefficient| = 1`. Pivoting on ±1 makes
  `inv = 1/coeff` exact and integral, introducing no new denominators --
  the exact-arithmetic analogue of numerical stability, and the thing that
  keeps H2's fast path *on* its fast path.
- Tie-break 2: smallest index (determinism, and the Bland fallback stays
  reachable).
- Partial pricing: cap the candidate scan at *k* entries with a rotating
  start (standard practice per Bixby's survey), bounding selection cost on
  the 691-term rows latendresse actually has.
- Termination: unchanged. Reuse the existing per-call pivot-count
  threshold that switches to pure Bland selection; the paper's termination
  argument applies to the Bland suffix exactly as it already does for
  Dantzig.

**Risk: low.** Purely a selection change inside `check()`; no new state, no
trail interaction, no explanation change. The real risk is *empirical*:
cheaper pivots but more of them. That is why `:lra.pivots` must be reported
alongside wall time in the evaluation, and why it lands opt-in.

**Falsifier:** if pivot counts rise more than per-pivot cost falls on
clock_synchro/miplib, the rule is net-negative and should be rejected
outright rather than tuned -- item 8's cost-ranking episode is the
cautionary precedent for tuning a heuristic that was steering search wrong.

**Falsifier outcome: fired on `miplib/pp08a` for the entering-side rule
alone** (pivots +43%, term-ops +19%, 1.63s -> 1.82s) -- and that was the
right signal to keep measuring rather than to tune. Adding the leaving-side
half turned the same instance into 1.36s, better than Bland, and the broad
sample settled the question decisively at +24 files with no regressions.
The lesson is the one item 8 taught, applied correctly: take the broad
sample's verdict, and neither tune a losing heuristic nor discard one on a
single instance.

### H7 -- Fixed-variable elimination (equality slacks)

**Status: designed 2026-07-29, implemented 2026-07-30, and NOT promoted --
it ships opt-in behind `--lra-fixed-elimination` (`eed259c` .. `ebd2533`).**
It did not clear the 595-file scrambled bar, and its row-freezing half was
deleted outright. See the falsifier outcome at the end of this item; that is
the part worth reading. This is also the first item in this note whose
evidence comes from a set Primo was never tuned against.

**Where it comes from.** On the 595-file scrambled QF_LRA competition set at a
60s timeout, Primo solves 373 against z3's 429 and cvc5's 351 -- ahead of cvc5,
behind z3, with zero cross-solver disagreements. The whole deficit sits in one
size band:

| file size | n | primo | z3 | cvc5 |
|---|---|---|---|---|
| 0-50 KB | 182 | 180 | 181 | 181 |
| 50-500 KB | 191 | **142** | 134 | 122 |
| 500 KB - 5 MB | 173 | **43** | **100** | 43 |
| > 5 MB | 49 | 8 | 14 | 5 |

Primo leads *both* references below 500 KB and collapses above it, landing on
exactly cvc5's number in the 500 KB-5 MB band while z3 more than doubles them.

**It is not a preprocessing size problem.** That was the first hypothesis and
it is wrong: on four profiled gap instances Primo's prepared tableau matches
z3's reported dimensions within ~5% every time (9,315x11,633 against z3's
9,000x11,330; 8,960x10,479 against 8,892x10,415; and so on). Primo also already
folds the scrambler's `(* 1.0 t)` / `(+ t 0.0)` obfuscation -- checked
directly, term size and solve-eqs counts are identical with and without it.
Both solvers build the same problem.

**The difference is what happens to equalities.** These instances are dominated
by them -- 8,226 to 40,820 lowered Real equalities per file, against 4,822 to
24,976 tableau rows, so most rows exist because of an equality. Primo lowers
`t1 = t2` to `t1 <= t2` and `t2 <= t1`; since item H1b those two normalize onto
one shared slack, which then receives an upper *and* a lower bound at the same
value. **That slack is fixed, and Primo does nothing with the fact** -- it stays
a live, pivotable row for the rest of the search.

z3 does something with it, and reports how much: `:arith-fixed-eqs` 168-52,364
and `:arith-offset-eqs` 855-68,937 per instance. Its search then barely runs --
on `scrambled126657` it needs **9 decisions and 4 conflicts** where Primo makes
141,124 decisions and 321M pivot term-operations. Profiling Primo there puts
86% of runtime in `LraTableau::pivot`, averaging 431 rows rewritten per pivot.

**Design, as implemented (commit `eed259c`).** The mechanism turned out to be
much smaller than the design sketch above suggested, because of one property
the tableau already had: **`can_enter()` already rejects a fixed variable.**
With `lower == upper == assignment`, every branch of its `assignment < upper` /
`assignment > lower` test is false. A fixed *non-basic* variable is therefore
already completely inert -- it is never selected to enter, so its column never
moves. Only fixed *basic* variables churn, and the fix is to stop them being
basic.

So `assert_upper`/`assert_lower` now do exactly this when they make a basic
variable's bounds coincide:

1. **Pivot it out**, choosing an entering variable from its row, then
   `update()` it to its fixed value -- which is mandatory anyway, since
   non-basic variables must always sit within their bounds, and is precisely
   what makes it inert. No substitution pass and no constant column are needed:
   the new basic variable's dependence on the now-inert fixed variable *is* the
   constant term.
2. **Prefer an entering candidate with no bounds**, then the Markowitz
   occurrence count, then a `+/-1` coefficient -- and never one that is
   *itself* already fixed, which the other criteria actively steer into
   (a fixed slack tends to occur in few rows and to carry a unit
   coefficient). An unbounded basic variable's row can never be violated, so
   it never demands another repair pivot.

A third step -- **freezing** the resulting row out of the working set
entirely when its variable was provably unconstrainable -- was implemented,
measured, and removed. What it cost, and why it went, is below.

**Offset equalities (`x - y = c`) remain a follow-on**, matching z3's
`arith-offset-eqs`: merge the two columns rather than carrying both. Better
attempted only once fixed-variable elimination is measured, since the two
overlap.

**Risk, and where it actually lives.** The elimination pivot is a pure change
of representation -- the same thing `check()` does on every repair -- so it
cannot produce a wrong answer. `build_conflict` needs no change either: it
takes each row term's bound reason, so a fixed variable appearing in a row
contributes the asserting literal of the equality that fixed it, which is
exactly the right Farkas antecedent.

Freezing was different, and that asymmetry is the whole reason the two halves
were built with separate switches. Its soundness reduced to a single
condition: **only freeze a row whose basic variable has no bounds and can
never acquire any.** "Unbounded right now" was not sufficient. A frozen row is
deliberately dropped out of the machinery that maintains it, so its own
assignment stops tracking its terms and its terms are no longer kept
non-basic; a bound asserted against it later would be tested against a stale
value. Only the caller knows which variables can ever be bounded -- it owns
the atom-to-tableau-variable map -- so `LraTableau::mark_boundable()` made it
say so, and an assert against a frozen row threw rather than answering from a
stale assignment.

None of that machinery survives (`ebd2533`), so **the retained mechanism has
no wrong-answer mode at all**: a pivot is what `check()` already does on every
repair. The guard is recorded here because its mutation check is the evidence
that removing it was the right call rather than a lucky one -- it was
genuinely load-bearing, so keeping the feature would have meant keeping the
risk:

- Dropping it entirely (freeze any row): caught by its dedicated unit test,
  *and* it makes an existing end-to-end test return the wrong verdict
  (`gaussian_elimination_can_be_disabled` goes from sat to not-sat). So the
  condition is genuinely load-bearing for soundness, not just for tidiness.
- Dropping only the `mark_boundable` half, keeping "unbounded right now" --
  the plausible-looking weaker guard: caught by the unit test, and on real
  benchmarks the throw fired immediately (variable 1133 on `scrambled335326`,
  1584 on `latendresse/ecoliFBAyicesTest-500`). That is the case that would
  otherwise have been a silent stale-value read.

**Falsifier.** If eliminating fixed variables does not reduce
`:lra.pivot-row-updates` proportionally to the share of rows eliminated, then
the fill-in is not concentrated in those rows and the diagnosis above is wrong.

**Falsifier outcome: the mechanism works and the item still loses.** This is
the interesting part of H7, and the reason it is written up at length despite
not shipping on by default.

The *local* evidence was strong. Paired A/B on the profiled instances
(workstation, 120s cap):

| instance | H7 off | H7 on | `:lra.pivot-term-ops` off -> on |
|---|---|---|---|
| `latendresse/ecoliFBAyicesTest-500` | 0.8s | **0.1s** | 8.66M -> 0.13M |
| `scrambled335326` | 51.0s | **14.9s** | 195M -> 27M |
| `scrambled126657` | timeout | timeout | 492M -> 40M in the same 120s |
| `scrambled412738` | timeout | timeout | 238M -> 140M |

An 8x, a 3.4x, and an order of magnitude less pivot work on instances that
still time out. The diagnosis was right: these tableaus really are dominated
by fixed equality slacks, and eliminating them really does remove most of the
pivoting.

**It does not convert.** On the 595-file scrambled set at 60s (job 117487, 3
Primo configurations x 20 shards, z3/cvc5 reused unchanged):

| config | solved | PAR2 | 0-50KB | 50-500KB | 500KB-5MB | >5MB |
|---|---|---|---|---|---|---|
| H7 off | 373 | 29204 | 177 | **144** | 45 | 7 |
| H7 on | 374 | 29237 | 179 | 140 | 41 | **14** |
| H7 on, freezing off | 375 | 29195 | 179 | 138 | 45 | 13 |
| z3 | 429 | 23138 | 178 | 136 | **101** | 14 |

**+1 file on 595.** PAR2 flat. The churn underneath is +19/-18 -- symmetric --
and over the 139 files both configurations solve in more than a second the
**median speedup is 0.94x**, a slight net *slowdown*. Big wins (4.88x, 3.31x,
3.26x) are matched by big losses (0.18x, 0.19x, 0.23x). On the 237-file
stratified sample the same picture: 167 -> 168, PAR2 1559 -> 1532. Zero
cross-solver disagreements on either set.

There is one real signal: **the >5MB band doubles, 7 -> 14 of 50, exactly
matching z3's own 14** -- the band most dominated by equalities, which is
where the diagnosis said the effect would be. It is paid for in the 50-500KB
band (144 -> 140), and the trade nets out.

So H7 ships opt-in. **No size or row-length threshold was added** to keep the
>5MB win while dodging the mid-range cost: that is tuning a heuristic that
lost its own bar, which is exactly item 8's mistake.

**The freezing half was deleted, not left behind a flag.** It is strictly
negative in the table above -- four files lost in the 500KB-5MB band (41
against 45) for one gained elsewhere -- while carrying every bit of H7's
wrong-answer risk. 352 lines removed against 106 added.

**What this says about the remaining gap.** The 56-file deficit against z3 is
*not* mostly the fixed-equality effect, even though that effect is real and
large per-instance. Two candidates remain, and the honest position is that
neither has been tested: offset equalities (`x - y = c` as a column merge,
z3's other counter, 855-68,937 per instance) and the possibility that the
mid-range cost here is the elimination pivots' own price on tableaus where
fixed rows were never the bottleneck. Recorded rather than chased.

### H4 -- Batched bound assertion (deferred `update()`)

**Targets:** Class C (DTP-Scheduling, tta_startup, TM/sc/uart).

**Problem.** Every asserted bound on a nonbasic variable immediately calls
`update()`, which walks `occurs_in_[var]` and rewrites every dependent
basic variable's assignment -- up to 2,127 rows for one variable on
`constraints-tempo-width-30`. CaDiCaL delivers assignments in *batches*
(`notify_assignment(const std::vector<int>&)`), and roadmap item 6 already
established that the repair (`check()`) can be deferred to
`cb_has_external_clause`. The assignment *move* can be deferred the same
way, and for the same verified reason.

**Design.** Split `assert_upper`/`assert_lower` into (a) the eager,
O(1) part -- record the bound, detect a direct
`lower > upper` contradiction, push the trail entry -- and (b) the deferred
part, the nonbasic assignment move. Queue the pending moves; at the
deferred-check point, coalesce them per variable (a variable bounded twice
in one batch moves once) and apply them in a **single sweep** over the
union of affected rows, so each affected row's assignment is recomputed
once per batch instead of once per assertion.

**Risk: moderate.** It changes when the "assignment satisfies all nonbasic
bounds" invariant holds, which is the invariant `check()`, propagation, and
`build_conflict` all rest on. The deferral point must be *exactly* item 6's
(`run_deferred_check_if_needed`), and the eager direct-conflict branch must
keep clearing the pending queue the same way item 6's fix had to clear
`needs_check_` -- that bug is the direct precedent for what goes wrong
here. Same-slack/row-based propagation read *bounds*, not assignments, so
they are unaffected (item 6 already verified that property).

### H5 -- Model-based phase selection (replacing item 8's variable override)

**Targets:** the TM/sc/uart cluster, without item 8's regressions
elsewhere.

**Problem.** Item 8's `cb_decide` round-robin was the roadmap's most
dramatic narrow win (7/9 files 40-97% faster, `sc-27` newly solved) and a
net *loss* on the 237-file sample (108 → 106, 4 regressions). The reason is
structural: it overrides **which variable** CaDiCaL decides, discarding
VSIDS's conflict-driven focus. That is a good trade only when conflicts are
rare and the formula is a chain of similar guarded atoms -- i.e. exactly
TM/sc/uart, and exactly not LassoRanker/sal/tta_startup.

**Design.** Keep VSIDS's *variable* choice; supply only the *polarity*.
For an atom bounding variable `v` at threshold `c`, the tableau's current
assignment β already tells us whether the atom holds under the current
model. Choosing the polarity that β satisfies means the resulting
`assert_upper`/`assert_lower` needs **no assignment move and no pivot at
all** -- the bound is already satisfied. That is a genuine
"free decision", and unlike item 8's round-robin it is a hint, not an
override: VSIDS keeps its ordering, so a family whose search needs VSIDS
focus loses nothing.

Mechanism: CaDiCaL's `phase(lit)`/`unphase(lit)` forced-phase API, already
exposed by the adapter as `CadicalBackend::phase(Lit)`
(`src/sat/cadical_backend.cpp`). Refresh phases lazily for the atoms whose
truth under β changed, at the same quiescence point item 6 uses -- not per
assignment.

**Interpretation to test:** the hypothesis is that item 8's win came mostly
from "decide atoms that need no theory work", and only incidentally from
the always-TRUE polarity happening to fit TM/sc/uart's guard structure. If
so, β-based phases should reproduce most of the 9-file win *and* be neutral
on the 4 files item 8 regressed. If instead the win came from the stable
*order*, this will show up as a much smaller 9-file effect -- a clean,
informative negative result either way.

**Risk: low.** Phase hints cannot affect soundness; the worst case is a
worse search order. Keep `--lra-theory-decisions` as-is (it stays a useful
lever for its file shape) and add this as a separate flag rather than
replacing it.

### H6 -- A difference-logic fast path

**Targets:** DTP-Scheduling (4 gap files, avg row exactly 2.00, max row 2 --
**100% difference constraints**), clock_synchro (4), tta_startup (5).

**Problem.** `constraints-tempo-width-30` is a QF_RDL problem wearing a
QF_LRA label: 3,932 rows, every one of the form `x - y`. Simplex on it
needs 60,750 decisions and 19.5s where z3 takes 0.71s -- and Section 1.3
shows its rows densifying from length 2 to length 33 with one variable
reaching 2,127 occurrences. Fill-in on a problem that has *no* inherent
fill-in is a representation failure, not a tuning problem.

**Design, two tiers.**

- **Tier 1 (cheap, and useful regardless):** detect the fragment at
  `PreparedLraTheory` construction -- every canonical polynomial has
  exactly two terms with coefficients `+1`/`-1` -- and expose it as a stat.
  This alone tells the evaluation which files H6 could ever help, and
  H1/H3 are likely to recover a large part of the gap without any new
  solver. Note H3's fill-in-aware rule is *specifically* well-suited here:
  a difference tableau has an ordering that keeps rows short.
- **Tier 2 (milestone-sized, out of scope for this note's first pass):** a
  real incremental negative-cycle solver for the fragment. The plan already
  exists as DL0-DL4 in [](papers/difference-constraint-propagation.md)
  (Cotton & Maler, "Fast and Flexible Difference Constraint Propagation
  for DPLL(T)", SAT 2006), and `src/theory/arithmetic/difference_logic.hpp`
  is already reserved for it. Its explanations (the edges of the negative
  cycle) are minimal by construction, which is exactly what the DPLL(T)
  layer wants.

**Risk:** Tier 1 is trivial and safe. Tier 2 is a new theory solver with
its own correctness surface and should not be started until H1-H3 have been
measured, since they may move these families on their own.

## 3. What is deliberately *not* proposed

- **More pricing rules for their own sake** (Devex, steepest edge). The
  roadmap already found neither Dantzig nor a full SOI implementation moved
  the narrow sample, and Section 1.2 says the per-pivot *cost*, not the
  pivot *count*, is what dominates. Devex approximates steepest-edge
  pricing to reduce iteration counts -- the wrong axis for this profile.
  H3 attacks the axis the measurements point at.
- **A full implied-equality basis** (Bromberger/Weidenbach). Already
  considered and set aside during item 7 as paper-sized with no measured
  SMT speedup; nothing in this profile changes that.
- **Anything gated on a per-family structural classifier.** Item 8's
  outcome is the argument: a heuristic that must be told which family it is
  running on is a portfolio entry, not a default.

## 4. Suggested order of work, and the evaluation each item needs

### 4.0 Re-ranking after H2 and H1 (2026-07-28)

The original ranking below was written against the Section 1.2 profile, in
which `LraTableau::pivot` was 31.4% of runtime and the allocator 42%. Both
H2 and H1 have since landed, so that profile no longer describes the solver.
Re-profiling `DTP-Scheduling/constraints-tempo-width-30` at the current head:

```
75.8%  LraTableau::pivot          (was 31.4%)
 5.0%  allocator                  (was 42.0%)
 ~8%   util::Rational operators and construction
```

**H3 is now unambiguously the next item, and by a wider margin than the
original ranking gave it.** Three independent lines of evidence converge:

1. `pivot()` went from a third of runtime to three quarters. H2 and H1
   removed the costs *around* it; nothing removed fill-in.
2. H1's own falsifier fired on the Class A instance:
   `latendresse/ecoliFBAyicesTest-500` lost 69% of its rows and did not speed
   up at all, so for the one family that is pure LP, row count was never the
   cost. H3 is the only remaining lever there -- and latendresse is still
   0/15 while z3 solves 6.
3. H1b's DTP-Scheduling regression is a fill-in effect: merging proportional
   rows cut decisions 8.6-fold and pivots to 4,248, yet wall time rose,
   because the surviving rows are denser. A cost-aware entering-variable rule
   is what that asks for.

The rest of the order is unchanged (**H5 → H4 → H6**), with one revision:
H4's premise was "~320 µs per decision" on DTP-Scheduling, measured when
that instance took 19.5s. It now takes ~6-8s with far fewer decisions, so
H4's expected value has dropped and it should be re-measured before being
picked up rather than implemented on the old number.

### 4.1 Original ranking

Recommended sequence: **H2 → H1 → H3 → H5 → H4 → H6 (Tier 1)**.

H2 first because it is the largest measured share, benefits every other
item, and is a self-contained change to one well-encapsulated type. H1
next because it multiplies with H2 (fewer rows × cheaper rationals) and
subsumes roadmap item 3. H3 after both, since its benefit is measured
relative to a pivot cost that H2 will have already changed -- measuring it
first would attribute H2's win to it.

Per the project's standing discipline, each item needs:

1. unit tests for the specific invariant it introduces (not only generic
   correctness examples);
2. `make check` plus the ASAN/UBSan suite;
3. `:lra.pivots` / `:lra.online-check-calls` / `:sat.decisions` /
   `:sat.conflicts` reported alongside wall time, so a "cheaper but more of
   them" outcome is visible rather than hidden;
4. a Slurm merge-gate fuzz run (500 cases) -- and, for H1/H2/H4 (which
   change unconditional core behavior), a campaign-gate run (3000 fuzzsmt
   cases plus opfuzz/yinyang mutation) on **both** the Bland and SOI paths,
   matching the precedent set by the `occurs_in_` and `violated_vars_`
   fixes;
5. **the 237-file stratified sample as the promotion bar**, diffed against
   the existing unchanged `z3.csv`/`cvc5.csv` from job 114200 (per the
   standing practice of not re-running unchanged reference solvers), and
   rendered with `scripts/render_solver_compare_html.py`;
6. a `docs/run-log.json` entry before the result is cited anywhere.

New stats worth adding alongside the implementation, since Section 1.3 had
to be produced with throwaway instrumentation: `:lra.rows`,
`:lra.tableau-vars`, `:lra.row-nonzeros` (post-preparation), and a
periodic or final fill-in figure. Their absence is why the fill-in
explosion went unnoticed through eight roadmap items.

## 5. Evidence trail

The **design** measurements in Sections 1.1-1.3 are local (workstation,
Release build, commit `35878c4`), recorded in `docs/run-log.json` under
`2026-07-27-qf-lra-heuristics-design-profiling`. They were profiling evidence
for a design decision, not benchmark claims.

Every **result** claimed for an implemented item comes from Slurm, per
AGENTS.md, and has its own run-log entry:

| item | implementation | gates | promotion-bar run |
|---|---|---|---|
| H2 | `02e2d47` | 114896-114899 | job 114900 |
| H1a | `077beec` | 115168-115171 | job 115172 |
| H1b | `1e6554d` | 116545-116548 | job 117222 (`bland` column) |
| H3 | `cf491ce`, promoted `dd60655` | 117219-117221, 117227-117228 | job 117222 |

Plus a milestone-wide campaign sweep (jobs 117248-117252) over every other
reachable configuration -- `bland`, `dantzig`, `--no-lra-sparse-leaving`,
`--no-lra-propagation`, and row/bidirectional propagation -- so that no
supported option combination is left un-fuzzed at campaign tier.

Local-only evidence that remains local, and is labelled as such where it
appears: the paired A/B timings, the tableau-shape row counts, and the
32-bit-vs-64-bit width ablation.

## 6. Where the gap is after H7 (2026-07-30)

Written immediately after the H7 result, while the measurements are fresh.
**Nothing here is implemented**; it is the diagnosis the next item should be
designed against, and it deliberately records what has been *ruled out* as
well as what is suspected.

### 6.1 The gap is one band, and it is z3-specific

Of the 76 files z3 solves and Primo does not (595-file scrambled set, 60s),
**59 are in the 500KB-5MB band** -- 8 in 50-500KB, 7 above 5MB, 2 below 50KB.
Two things sharpen that:

- **cvc5 solves only 9 of those 59.** This is not "hard for everyone"; it is
  something z3 does that neither other solver does.
- **z3 is not comfortable there either.** Its median time on the 59 is 25.0s
  of a 60s budget (min 1.7s, max 56.2s). A longer competition timeout would
  likely recover a large share of them for Primo -- which is worth
  remembering before treating this as an algorithmic wall.

### 6.1a It is a constant factor, not a wall -- and the factor is small

Job 117631 re-ran exactly those 59 files at a 600s budget (two Primo
configurations x 20 shards; z3 was deliberately not re-run, since it solves
all 59 by construction).

**Primo solves 45 of the 59.** Only 8 resist both configurations. So this
band is overwhelmingly a constant-factor problem, and the constant is far
smaller than the 60s cliff suggested:

| | time on the 45 it solves |
|---|---|
| min / p25 / median / p75 / max | 64s / 114s / **167s** / 275s / 563s |

Against a 60s budget that is a **median 2.8x, p75 4.6x, worst 9.4x**. The
payoff is therefore close to linear in the speedup achieved, not
all-or-nothing:

| speedup | files recovered at 60s (of 59) |
|---|---|
| 2x | 15 |
| 3x | 24 |
| 5x | 37 |
| 10x | 45 |

This is the single most useful number in this section: **a 3x is worth 24
files and a 5x is worth 37**, so incremental progress on per-pivot cost pays
proportionally and there is no threshold effect to clear first. Against z3's
own 60s times on the same files Primo is a median 6.6x slower (p90 31.6x),
which is consistent.

The 14 files that do not solve even at 600s are the genuine capability
residue -- about a quarter of the band -- and should not be conflated with
the rest when judging any future mechanism.

**H7 does not convert at a longer budget either**: 41 solved against the
default's 45, +6/-10 churn. Its per-instance wins are real but land on
instances that were not the binding ones, which is the same conclusion the
60s run reached and independent confirmation that shipping it opt-in was
right.

### 6.2 The cost is fill-in, and both Markowitz factors blow up

Profiled on four instances chosen by *z3-time quartile* across the band (p10,
p25, p50, p75) rather than by how dramatic they look -- the H7 write-up
records why that distinction matters.

`:lra.pivot-row-updates` counts rows rewritten (the sum of
`|occurs_in(entering)|`), and `:lra.pivot-term-ops` counts terms merged while
rewriting them, so their ratio decomposes a pivot's cost into its two
Markowitz factors exactly:

| instance | rows rewritten per pivot | terms merged per rewrite | nonzeros/row when prepared |
|---|---|---|---|
| `scrambled228965` | 359 | 31.2 | 5.80 |
| `scrambled15659` | 26 | 21.7 | 4.10 |
| `scrambled312090` | **521** | **91.1** | 5.25 |
| `scrambled180721` | 35 | 44.4 | 4.53 |

Read the last two columns together: rows that hold **4.1-5.8 nonzeros in the
prepared tableau** are merging **21.7-91.1 terms** by the time they are being
rewritten during search. That is a 5-17x densification, and it is happening
*on top of* an entering column that occurs in up to 521 rows. One pivot on
`scrambled312090` therefore costs on the order of **47,000 rational
operations**, and the instance spends its entire 60s budget on 3,904 of them.

This is what "fill-in" means concretely, and why it compounds: every pivot
substitutes the entering variable's row into every row mentioning it, so each
one makes the *next* one more expensive. It is a positive feedback loop, not a
constant overhead.

Note also `scrambled312090`'s search shape: **0 conflicts, 7,555 decisions**.
The SAT solver is not thrashing and learns nothing; essentially all of the
time is inside the tableau.

### 6.3 What this rules out

- **It is not the sparse-pricing cap.** `kSparsePricingCandidateLimit` is 16
  and undocumented as a derived value, so raising it was the obvious cheap
  fix. Measured on `scrambled312090` at limits 16 / 64 / 256 / 1,000,000, the
  per-pivot cost is essentially flat -- 51,738 / 52,428 / 46,833 / 47,312
  term-ops per pivot across a 60,000x range. Scoring more candidates does not
  find cheaper ones, because there are none: when every eligible candidate
  occurs in hundreds of rows, a better choice among them does not exist. H3
  prices the pivot correctly; the problem is that the cheapest available pivot
  is already expensive.
- **It is not fixed-equality slacks.** That was H7, and it nets zero on this
  band (+8/-8, median 0.64x). See its falsifier outcome above.
- **It is not preprocessing size.** Established on 2026-07-29: prepared
  tableau dimensions match z3's within ~5%.

### 6.4 What z3 appears to be doing

z3's `-st` output on the same instances, with Primo's comparable counters:

| counter | `scrambled228965` | `scrambled15659` | `scrambled312090` |
|---|---|---|---|
| z3 `:arith-offset-eqs` | 139,942 | 259,413 | **1,100,682** |
| z3 `:arith-fixed-eqs` | 45,068 | 158,312 | 373,175 |
| z3 `:arith-bound-propagations-lp` | 302,614 | 498,156 | 1,206,041 |
| z3 `:decisions` | 21,656 | 158,793 | 36,778 |
| Primo `:sat.decisions` | 135,366 | 1,009,955 | 7,555 |
| Primo `:lra.pivot-term-ops` | 130.5M | 201.4M | 185.4M |

**Offset equalities dominate**, and by a wide margin on the largest instance.
But a row-shape census of the *prepared* tableau shows only 175 / 2,267 /
1,844 rows of literal `x - y = c` shape (3.4% / 15% / 7.6% of rows), against
z3's counts in the hundreds of thousands to millions. **So z3 is deriving
these dynamically during search, not recognising them syntactically up
front.** Any Primo version of this is therefore a search-time mechanism, not
a preprocessing pass -- which makes it materially bigger than H7 was, and
that should be priced in before committing to it.

### 6.5 Candidate directions -- ranked, with status

The open list for this band. **Nothing here is implemented.** Ranked by
expected value per unit of work, not by expected value alone, because section
6.1a showed the payoff is close to linear: a 3x is worth 24 files and a 5x is
worth 37, so a cheap partial win is genuinely worth more than an expensive
whole one.

| # | direction | expected size | cost | status |
|---|---|---|---|---|
| D1 | Evaluate existing row-based bound propagation on this band | unknown | **one benchmark run** | untried |
| D2 | Floating-point-guided basis selection (Monniaux) | large | large | surveyed, not planned |
| D3 | Dynamic offset-equality detection | largest | largest | designed nowhere |
| D4 | Revised simplex with an LU-factored basis | unknown | largest | open question |
| -- | ~~Periodic refactorization~~ | -- | -- | **rejected 2026-07-30** |

**D1. Evaluate the row-based bound propagation Primo already has.** z3 reports
`:arith-bound-propagations-lp` of 302k-1.2M on the profiled instances, so
propagation is doing heavy lifting there. Primo has both row-based and
bidirectional row propagation implemented (`--lra-row-propagation`,
`--lra-bidirectional-row-propagation`), but both are opt-in, off by default,
and **have never been evaluated on this band at all**. This is the only entry
that costs a benchmark run rather than an implementation, which is why it is
first regardless of expected size.

**D2. Floating-point-guided basis selection** (Monniaux, CAV 2009): run a
floating-point LP, then force-pivot the exact tableau to the basis it found.
Sound, because the numeric result only chooses a starting configuration and is
never trusted. Surveyed in [](papers/lra-related-work.md) and recorded there as
not planned, since it needs an incremental floating-point LP engine alongside
the exact tableau. **Promoted by section 6.2's finding**: density is a property
of the *basis*, and this is the one technique here that chooses the basis
directly rather than walking to it.

**D3. Dynamic offset-equality detection**, matching z3's dominant counter
(`:arith-offset-eqs`, up to 1.1M). Section 6.4 established it is derived
*during search*, not recognised syntactically -- only 3.4-15% of prepared rows
have literal `x - y = c` shape. So this is a search-time mechanism, materially
bigger than H7 was. Needs a design that states where detection hooks in and
what it costs on instances where it finds nothing, before any estimate of the
win is worth anything.

**D4. Revised simplex with an LU-factored basis.** The representation change
the rejected entry below turned out to require, and the hardest item here.
King's thesis records that no Simplex-for-DPLL(T) implementation does it.

*Scope.* Not a feature but a change of the data structure the class is built
on. `LraTableau` is ~1,680 lines with 18 internal uses of `rows_` and 29 of
`occurs_in_`; in revised simplex **neither concept exists**. A row is
materialised on demand as `e_i^T B^-1 N` (BTRAN plus a pass over N), a column
as `B^-1 A_j` (FTRAN). Consequences, which differ per subsystem:

- `update()` *improves* -- walking `occurs_in_` to update dependent basic
  variables is exactly an FTRAN.
- `build_conflict` is fine: one BTRAN per conflict to materialise the violated
  row for the Farkas certificate.
- **Row-based bound propagation breaks entirely** -- it iterates
  `occurs_in(var)` and then reads rows, and neither survives. Note this
  collides with D1.
- **SOI breaks** -- its objective row is explicit and incrementally maintained.
- **push/pop is the crux.** The trail snapshots `old_basic_row` and
  `old_other_rows` per pivot; there is no un-applying an LU update, so a pop
  means refactorizing. LP solves one problem to optimality, DPLL(T) pops
  thousands of times. One mercy: asserting a bound does not change `B` (bounds
  are on variables, not rows), so the basis matrix is stable across most of
  the search.

*The likely killer is arithmetic, not architecture.* In floating point LU
entries stay bounded; in **exact rational** arithmetic LU factors suffer
coefficient growth, so a sparser factored representation can still lose to a
denser explicit tableau because every entry costs more. That this is real and
studied is evidenced by integer-preserving (Bareiss-style) factorization and
SPEX existing at all -- see "Expediting exact linear programming solvers via
integer preserving factorization" (Operations Research Letters, 2025). This,
rather than oversight, is the plausible reason nobody in SMT does it.

*Cheap first step, which is not building it.* Take a prepared tableau from one
of the four profiled instances, compute an exact LU with a Markowitz ordering
offline (sympy, or SPEX), and compare `nnz x entry bit-size` against the
explicit tableau's. About a day, touches no solver code, and either kills D4
or makes it credible enough to justify the rewrite.

**Rejected: periodic refactorization to discard accumulated fill-in**
(2026-07-30, before implementation). In *revised* simplex the basis is an LU
factorization updated per pivot (Forrest-Tomlin, Bartels-Golub); those updates
accumulate fill-in and numerical error beyond a fresh factorization, and
refactorization discards exactly that surplus. Primo stores the **explicit
tableau**, which for a given basic/non-basic partition is `B^-1 N` --
*uniquely determined*. Recomputing it from the original rows returns the
identical matrix at identical density. The 5-17x densification in section 6.2
is a property of the **basis reached**, not recoverable garbage from the path
taken to it, so there is nothing to discard. See
[](papers/lra-related-work.md)'s refactorization note.

*The lesson generalises, which is why this is kept rather than deleted:*
"measured headroom" is only headroom if some mechanism can actually recover
it. The 5-17x looked like an opportunity and was a description of the problem.

**A falsifier for the whole diagnosis, stated in advance:** if a mechanism
reduces `:lra.pivot-term-ops` per pivot substantially on these four instances
and the band's solved count does not move, then per-pivot cost is not what
separates Primo from z3 here, and the remaining explanation is search *order*
(z3 needs 36,778 decisions where Primo needs 7,555 but never finishes -- the
shapes are not comparable, which is itself a hint worth chasing).
