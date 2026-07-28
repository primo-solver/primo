# QF_LRA Speedup Heuristics: Design Note

Status: **design only** (2026-07-27). Nothing here is implemented yet. This
note picks up where the competitiveness roadmap's items 0-8 (see
[](arithmetic.md)'s Milestone roadmap) left off, and is grounded in a fresh
local profiling/instrumentation pass run at commit `35878c4` -- not in the
older 3-file evidence items 0-6 were designed against.

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

### H2 -- Small-rational fast path in `util::Rational`

**Status: implemented, unconditional, 2026-07-28.** See
[](arithmetic.md)'s "Two-tier rational representation" for the shipped design
and `docs/run-log.json` for the verification trail. Paired local A/B on the
profiled instances: **2.6x-4.5x faster**, with every search counter
bit-identical (a pure representation change). Two design points below were
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
  knob so the two can be compared empirically. Deferred ablation.

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

**Measured: 2.6x-4.5x** on the profiled instances -- better than expected,
because the allocator cost was compounding across `DeltaRational` (two
`Rational`s per bound and per assignment) rather than being paid once.

### H3 -- Fill-in-aware (Markowitz-style) entering-variable selection

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

All measurements in this note are local (workstation, Release build,
commit `35878c4`) and are recorded in `docs/run-log.json` under
`2026-07-27-qf-lra-heuristics-design-profiling`. They are **local profiling
evidence for a design decision**, not benchmark claims -- any performance
result from implementing these items must come from Slurm per AGENTS.md.
