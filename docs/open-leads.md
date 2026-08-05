# Open leads register

A single place to find every live idea for improving Primo's arithmetic
performance, what evidence supports it, what it would cost, and -- stated
before the work starts -- what result would kill it.

It exists because leads were accumulating scattered across
[](qf-lra-heuristics.md), [](papers/lra-related-work.md),
[](tropical-matrix-structure.md), [](preprocessing.md) and individual paper
notes, which made it easy to re-propose something already rejected and easy to
lose something cheap. This register is the index; the detail stays in the
notes it points to.

## How to use it

- **Before proposing work**, check the [rejected](#rejected) section. Several
  entries there look obviously right and are measurably wrong.
- **Before implementing**, check that the lead's precondition still holds --
  entries record what was true when measured, and the solver moves.
- **Every entry states a falsifier in advance.** That is the project's habit
  (see H11 and D1), and it is what lets a negative result close a lead instead
  of inviting a retune.
- **Ranking is expected value per unit of work**, not expected value. A cheap
  partial win outranks an expensive whole one; [](qf-lra-heuristics.md)
  section 6.1a is the argument.

## At a glance

| # | lead | expected size | cost | status |
|---|---|---|---|---|
| [1](#lead-pure-filter-lra) | Extend `--pure-theory-filter` to LRA atoms | unknown, plausibly large on guarded families | **small** | open, top of queue |
| [2](#lead-flag-first) | Flag-first decision ordering (invert `cb_decide`) | unknown | **small** | open |
| [3](#lead-leaving-rule) | MathSAT's real leaving rule: least-violated, short-row tie-break | 1.42x median in its source | **small** | open |
| [4](#lead-aep) | Adaptive Early Pruning | ~20% total time in its source | small-medium | open |
| [5](#lead-relevancy) | Relevancy propagation | unknown, principled | **large** | open, gated on 1 |
| [6](#lead-justification) | Justification / goal-directed decision heuristics | unknown | medium-large | open, no paper note yet |
| [7](#lead-row-representation) | Compare row representation against sorted arrays of pairs | ~1.20x median in its source | small | open |
| [8](#lead-dl6) | DL6: decide precision statically before search | unknown | small | blocked on a DL solver |
| [9](#lead-monotone) | Monotone (one-sided) variable elimination | neutral on the promotion bar | done | **implemented, opt-in; did not clear its bar** |
| -- | D2 floating-point-guided basis selection | ~14% in the literature | medium | open, see [](qf-lra-heuristics.md) |
| -- | D4 revised simplex, LU-factored basis | unknown | largest | open, see [](qf-lra-heuristics.md) |
| -- | H4 batched bound assertion; H6 DL fast path; H11 fill-in-aware preprocessing elimination | -- | -- | design only, see [](qf-lra-heuristics.md) |

(lead-pure-filter-lra)=
## 1. Extend `--pure-theory-filter` to LRA atoms

**Claim.** Primo already computes, and already reports, that most theory atoms
on guarded families occur with a single polarity -- then filters none of them,
because the mechanism is implemented for QF_UF only.

**Evidence.** On `tropical-matrix/constraint-199552.smt2`:

| counter | value |
|---|---:|
| `:euf.theory-atoms` | 3,846 |
| `:euf.pure-atoms.positive` | **2,892 (75%)** |
| `:euf.pure-atoms.negative` | 0 |
| `:euf.pure-filter.enabled` | **0** |
| `:euf.pure-literals.filtered` | **0** |

Ablation confirms inertness on this family: `--pure-theory-filter=on` 4.152s
against `off` 4.150s. The analyzer independently agrees the atoms are
single-polarity: 2,244 distinct atoms, all pure-positive.

**Why it is sound.** The same argument as the EUF case ([](euf.md), from
Sebastiani's survey §6.10): an atom occurring only positively, assigned false,
cannot be what satisfies any clause, so the theory need not check that
assignment's consistency. It omits a constraint from a check rather than adding
one -- which is exactly why it is sound where the pure-literal rule is not, see
[](preprocessing.md)'s [](#pure-literal-trap).

**Cost.** Small. The polarity analysis exists and runs; the gap is applying it
to arithmetic atoms.

**The trap to get right.** Filtered literals must never be used as
theory-deduced premises -- they were deliberately not given to the theory
solver. [](euf.md) already documents this for EUF; the LRA propagation path
needs the same discipline, including propagation *candidates* whose literal
would be filtered.

**Falsifier.** If enabling it on the guarded families does not reduce
`:lra.pivots` materially, then the irrelevant atoms were not reaching the
tableau in the first place and the whole relevance line -- including lead 5 --
is worth less than it looks. That is a cheap and highly informative negative.

(lead-flag-first)=
## 2. Flag-first decision ordering

**Claim.** On guarded families the Boolean layer is self-contained and decides
which arithmetic is relevant; deciding arithmetic first is backwards.

**Evidence.** On tropical-matrix the Boolean layer is 468 unconditional
definitions over 36 free inputs, referring to no arithmetic at all
([](tropical-matrix-structure.md)). The solved run shows 15,766 decisions
against **93 conflicts** -- the search is walking, not fighting, and doing
tableau work along the way. Decisively, the existing `--lra-theory-decisions`
does the *opposite* (its `cb_decide()` loop skips every atom where
`!prepared_atom.is_arithmetic`) and is catastrophic here:

| config on `constraint-199552` | result |
|---|---|
| `--guarded-real-equality-lowering` | **3.5s**, sat |
| the same plus `--lra-theory-decisions` | **timeout at 90s**, 9,185,550 theory decisions |

**Cost.** Small: the same `cb_decide()` hook with the filter inverted. The
existing option is a ready-made control to A/B against.

**Falsifier.** If inverting the filter does not beat both the default and
`--lra-theory-decisions` on the guarded families, the ordering hypothesis is
wrong and the 9.2M-decision result was only telling us that round-robin
arithmetic decisions are bad -- which we would already know.

(lead-leaving-rule)=
## 3. MathSAT's actual leaving-variable rule

**Claim.** Primo has never tried the rule its own docs attribute to Griggio.
The primary source (thesis §2.5.3, p. 53) picks the violated basic variable
**closest** to its violated bound, with **short row** as the tie-break. Primo
offers most-violated-first (default) and shortest-row-first (opt-in
`--lra-sparse-leaving`) -- each inverts one half of it.

**Evidence.** The thesis measures its greedy rule at **1.42x median / 2.77x
mean** against Bland, on SMT-LIB QF_LRA. Full context in
[](papers/griggio-mathsat-thesis.md).

**Cost.** Small -- a comparator inside an existing selection loop.

**Caveat, recorded so it is not skipped.** The thesis is Dec 2009 and describes
MathSAT **4**; King (2014) attributes most-violated to MathSAT**5**. The rule
may genuinely have changed, so this is a third design point to test, not a
correction to apply blindly. Implement the rule as stated rather than flipping
one comparator and assuming that reproduces it. Worth varying the Bland
fallback threshold alongside it: MathSAT used the number of input variables,
Primo uses `4 * vars_.size() + 64`.

**Falsifier.** No improvement in solved count or PAR2 on the 237-file
stratified sample against both existing leaving policies.

(lead-aep)=
## 4. Adaptive Early Pruning

**Claim.** Adapt the *frequency* of theory calls to their measured usefulness,
rather than using a cheap incomplete solver for them.

**Evidence.** Griggio thesis §2.3.1 (p. 42), presented there as not described
elsewhere: a `tsolvers_call_interval` counter, reset to 1 on a theory conflict,
halved when the call produced deductions, doubled after two consecutive useless
calls. Reported at 4 more instances solved and up to ~20% less total time on
SMT-COMP'09, on both sat and unsat sets.

**Cost.** Small-medium: a counter and four rules, but wired into the
DPLL/theory interaction, which is where correctness bugs hide.

**Falsifier.** If `:lra.online-check-calls` is not a material share of runtime
on the target families, there is nothing to save. **Check this first** -- it is
one stats run and it gates the whole item.

(lead-relevancy)=
## 5. Relevancy propagation

**Claim.** Do not assert theory atoms that the current partial assignment has
made irrelevant. z3's mechanism (de Moura and Bjorner); absent from Primo and
from [](papers/lra-related-work.md) entirely.

**Evidence.** Indirect but strong. The tropical encoding puts 468 arithmetic
definitions under guards, and the unguarding experiment shows what happens when
that arithmetic is forced to be live: 27x slower, 68x the pivots
([](tropical-matrix-structure.md)). Whether Primo is *already* doing some of
that wasted work under the guarded encoding is exactly what lead 1 measures.

**Cost.** Large. It is a cross-cutting mechanism touching the SAT interface,
theory assertion, propagation and explanation.

**Sequencing.** Gated on lead 1, which is its cheap static approximation. If
filtering on polarity alone moves these families, dynamic relevance is worth
the investment; if it does not, that is evidence about the family before a
large mechanism gets built. Needs its own paper note first, per AGENTS.md.

(lead-justification)=
## 6. Justification / goal-directed decision heuristics

**Claim.** Choose decisions by working backward from what is needed to satisfy
the formula's definitional structure, rather than by VSIDS activity. CVC4's
`JustificationHeuristic`, with roots in ATPG (PODEM/FAN).

**Evidence.** Structural only, and weaker than the others: the guarded families
are one huge assertion built from thousands of definitional chains, which is
the shape this heuristic class targets. Lead 2 is a narrow, cheap special case
of the same intuition, which is a reason to do lead 2 first and treat its
outcome as evidence about this one.

**Status.** No paper note exists. Not actionable until one does.

(lead-row-representation)=
## 7. Compare the row representation against sorted arrays of pairs

**Claim.** Worth confirming Primo's row storage against the alternative the
Griggio thesis reports testing.

**Evidence.** Thesis §2.5.2 (p. 51): they tried per-equation hash tables and
sorted arrays of `(variable, coefficient)` pairs, and kept the arrays despite
logarithmic rather than amortized-constant coefficient lookup, because
contiguous layout and faster iteration matter more -- iteration being what
pivoting does. Measured at **1.20x median / 1.30x mean** for the wrong choice.
Unusual in reporting the losing alternative, which is what makes it useful.

**Cost.** Small to check what Primo currently does; larger to change it.

**Falsifier.** If Primo already stores rows as contiguous sorted pairs, this
closes immediately as already-done.

(lead-dl6)=
## 8. DL6 -- decide precision statically, before search

Recorded in full at [](papers/difference-constraint-propagation.md). For a
difference-logic solver based on negative-cycle detection, sum the absolute
values of the constants: if that fits the native integer type, no path or cycle
weight can overflow, so the whole computation can run overflow-check-free
rather than through the two-tier rational's per-operation check.

**Blocked** on `src/theory/arithmetic/difference_logic.hpp` being real (H6 Tier
2). Recorded now because it is a design constraint on a solver not yet written:
retrofitting a check-free path is harder than designing for one.

**Falsifier, stated in advance.** If the sum overflows on most real DL
benchmarks the pass is dead weight. Compute it over `DTP-Scheduling`,
`clock_synchro` and `tta_startup` before writing solver code.

(lead-monotone)=
## 9. Monotone (one-sided) variable elimination

**Claim.** If every constraint mentioning a real variable pushes it the same
direction -- after accounting for Boolean polarity -- that variable can be sent
to its extreme and those constraints discharged. This is the Fourier-Motzkin
observation that eliminating a single-sign variable generates **zero** new
constraints, and the LP-presolve notion of a dominated column.

**This lead was closed on 2026-08-05 and reopened the same day.** The closure
rested on a screen of *one file per family*, eight files, which found almost no
one-sided variables. Running `scripts/analyze_smt2.py` over the **entire local
QF_LRA corpus -- 1,753 files, all 17 families, zero analysis failures** --
contradicts it:

| family | files | reals | one-sided | share |
|---|---:|---:|---:|---:|
| keymaera | 21 | 89 | 29 | **32.6%** |
| meti-tarski | 488 | 1,480 | 365 | **24.7%** |
| 2017-Heizmann-UltimateInvariantSynthesis | 58 | 22,319 | 442 | 2.0% |
| LassoRanker | 419 | 4,455,966 | **85,120** | 1.9% |
| sal | 107 | 7,226 | 67 | 0.9% |
| DTP-Scheduling | 91 | 7,650 | 52 | 0.7% |
| TM | 25 | 14,531 | 41 | 0.3% |
| 2019-ezsmt | 105 | 91,853 | 10 | 0.0% |
| clock_synchro, latendresse, miplib, sc, tropical-matrix, uart, spider, check | 434 | 154,062 | 0 | 0% |
| **total** | **1,753** | **4,756,176** | **86,126** | **1.8%** |

**The population is real and has a mechanism.** The one-sided variables in the
Heizmann family are named `motzkin_*`: they are Farkas/Motzkin multipliers from
constraint-based invariant synthesis, which are non-negative by construction
and therefore appear only with one-directional bounds. LassoRanker is the same
lineage (ranking-function synthesis via Farkas' lemma), which is consistent with
its 85,120. These are not incidental variables; they are a structural feature of
how that whole class of benchmark is generated.

**Why it is interesting.** `LassoRanker` and `2017-Heizmann` are gap families --
[](qf-lra-heuristics.md) lists Heizmann/LassoRanker as "hard for everyone" --
and 85,120 eliminable columns is a large absolute number even at 1.9%.

**Cost.** Small-medium. The detection already exists in the analyzer; the work
is a preprocessing pass in the solver plus the polarity bookkeeping to make it
sound on non-NNF input.

**Two caveats, both to check before building anything.**

1. **The high-percentage families may not be where the time goes.**
   `meti-tarski` and `keymaera` have the best ratios but are families Primo
   already matches the reference solvers on, and `meti-tarski`'s files are tiny
   (3 reals each in the 100% cases). The value, if any, is in LassoRanker and
   Heizmann.
2. **Eliminating a variable is not automatically a win.** These are columns in
   an already-sparse tableau; removing them helps only if it removes rows or
   reduces fill-in. Measure the tableau shape before and after, not just the
   variable count.

**Falsifier, stated in advance.** Implement detection only, report
`:preprocessing.monotone-eliminable` on LassoRanker and Heizmann, and check it
against `:lra.rows` / `:lra.row-nonzeros`. If eliminating those columns does not
reduce rows or fill-in materially, the lead closes for real this time, and it
closes on solver counters rather than on a syntactic census.

### Outcome: implemented, and it did not clear the promotion bar

**Status: implemented behind `--monotone-elimination`, correctness-gated, and
NOT promoted.** The prototype numbers below did not survive a real sample.

Slurm job 119465, 237-file stratified sample, 10s bar, same binary both sides:

| configuration | solved | PAR2 | wrong |
|---|---:|---:|---:|
| default | **174** | **1415.10** | 0 |
| `--monotone-elimination` | 173 | 1432.84 | 0 |

One file lost, none gained. Over the 71 instances both configurations solve in
more than 0.2s the paired effect is **neutral** -- median ratio 0.997, p10
0.986, p90 1.014, 147.9s against 149.1s in total. The lost file
(`LassoRanker/.../byron-2...phaseTemplate`) is a genuine 3.5x slowdown rather
than boundary churn: re-run at 120s it takes 3.14s by default and 10.91s with
the pass. That single file accounts for 16.81 of the 17.75 PAR2 delta.

Worth recording precisely because it is the trap this register exists for: a
first cut of the analysis reported "faster on 57 files, slower on 1", which
counted sub-second instances where the ratio is noise. On non-trivial
instances the effect vanishes. Correctness held throughout -- zero wrong
answers here, and a clean fuzz gate at job 119430 (3,000 cases, 0 wrong
answers, 2,594 models validated, 0 invalid).

The pass stays available as an option and its machinery -- polarity-aware
one-sidedness detection, and postsolve model reconstruction -- is reusable.
But nothing about it is proposed for default on this evidence.

### Prototype measurement, 2026-08-05

Caveat 2 above has now been answered, and the answer corrects the framing that
produced it. **The saving is mostly in the Boolean structure, not the tableau.**
Replacing an atom by a constant removes it from the CNF and satisfies the
clauses containing it; dropping tableau rows is the secondary effect.

An external prototype (scratchpad, not committed) computes one-sidedness with
polarity and coefficient signs, then replaces every atom mentioning a one-sided
variable with the constant its own occurrence requires -- `true` at positive
polarity, `false` at negative. That is Fourier-Motzkin on a single-sign
variable: with no opposite bounds to pair against, every constraint mentioning
it is discharged outright.

**Soundness spot-check.** 24 files spanning the families with a population,
z3 on the original against z3 on the transformed file: **24 agree, 0 disagree**.

**Effect on files that take real time**, same binary, answers unchanged (both
`unsat`):

| file | atoms folded | clauses | rows | pivots | time |
|---|---:|---|---|---|---|
| Heizmann `_standard_copy4...bpl_7` | 192 | 2,909 -> **2,507** | 618 -> **543** | 146,833 -> 147,931 | 15.77s -> **14.15s** |
| LassoRanker `2013POPL-BenAmGen-Ex3.20...pieceTemplate` | 21 | 1,946 -> 1,925 | 622 -> 622 | 97,777 -> **69,828** | 12.19s -> **7.84s** |

The second row is the instructive one: **21** atoms folded, **no** row
reduction at all, and still 36% faster. The win there is not tableau size, it
is that a smaller Boolean structure produces a different and better search.
That also means caveat 2's proposed falsifier -- "if it does not reduce rows or
fill-in, close it" -- was the **wrong test**, and would have closed a lead that
works. Judge it on end-to-end time and `:sat.clauses`, not on `:lra.rows`.

On the smallest `meti-tarski` files the transformation discharges the problem
completely (`:sat.clauses` to 0, tableau gone), but those solve instantly
already, so they are not evidence of value.

**Revised effort estimate.** Detection and rewrite are both small; the real
cost is elsewhere and should not be discovered late:

- **Model reconstruction is the actual work.** Primo emits models
  (`(define-fun x () Real 0)`), and an eliminated variable is no longer in the
  tableau, so its value must be computed *after* the rest of the model is
  fixed, from the constraints that were discharged -- in dependency order,
  since those bounds can mention other variables including other eliminated
  ones.
- **The fuzz gate is mandatory**, since this changes preprocessing: merge tier
  at minimum, campaign tier if it is ever promoted to default. Per the standing
  rule, on Slurm rather than locally.

So: worth doing, tractable, and **not** a one-afternoon change. The honest
summary is a real but moderate speedup (roughly 10-36% on the two timed files),
available on maybe a quarter of the families, for a moderate implementation
with a genuine model-reconstruction obligation.

**Coverage note.** The corpus screen analyzed 1,753 of 1,753 files with no
timeouts and no memory failures, so no file was excluded and the denominator is
complete. Two families carry a real blind spot: `sc` (20.8% of atoms opaque)
and `tta_startup` (12.1%), both from `ite` inside arithmetic terms, where the
analyzer conservatively forces every variable two-sided. Their zeros therefore
mean "could not tell", not "none". Every other family is at 0.0% opaque.

(rejected)=
## Rejected and closed leads

Kept rather than deleted, because several are inviting and the negatives are
informative. Each names the evidence that closed it.

| lead | why it is closed |
|---|---|
| **Unguarding tropical's guarded definitions** | Sound but **27x slower** (4.13s -> 112s, 68x pivots). The guards are load-bearing: setting the flag discharges the definition at the Boolean level, so the case split is cheaper than the tableau row it avoids. [](tropical-matrix-structure.md) |
| **Row propagation on top of T1 (T2)** | 0/10 at every cap setting against the baseline's 1/10 -- a regression, not a wash. Well-controlled: the mechanism fired 2,088 times, and counters are *identical* at default/128/1024 caps because tropical never approaches even the default caps. [](qf-lra-heuristics.md) |
| ~~Monotone / one-sided variable elimination~~ | **Reopened 2026-08-05 on corpus-wide evidence -- see [](#lead-monotone). The 8-file screen that closed it was unrepresentative.** |
| **The propositional pure-literal rule on theory atoms** | **Unsound.** Counterexample verified: `(or (>= x 10) d) AND (<= x 0)` is sat, but forcing the pure-positive atom makes it unsat. Syntactic purity is not semantic independence. [](preprocessing.md)'s [](#pure-literal-trap) |
| **D1 row propagation, raised caps** (mid-band) | Rejected on a 59-file cluster sweep: no cap setting beat baseline on solved count. [](qf-lra-heuristics.md) |
| **D3 offset-equality detection** | Closed by D1's sweep -- they are the same mechanism. |
| **Periodic refactorization** | Rejected 2026-07-30 on a literature check. |
| **H7 fixed-variable elimination** | Implemented, did not clear its bar; kept opt-in. |

## Adding a lead

Give it: the claim in one sentence; the evidence, with counters or timings and
whether they are local or SLURM; a cost estimate; the sequencing constraint if
it depends on another lead; and **a falsifier stated before the work starts**.
A lead without a falsifier tends to get retuned instead of closed.
