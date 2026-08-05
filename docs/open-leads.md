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

(rejected)=
## Rejected and closed leads

Kept rather than deleted, because several are inviting and the negatives are
informative. Each names the evidence that closed it.

| lead | why it is closed |
|---|---|
| **Unguarding tropical's guarded definitions** | Sound but **27x slower** (4.13s -> 112s, 68x pivots). The guards are load-bearing: setting the flag discharges the definition at the Boolean level, so the case split is cheaper than the tableau row it avoids. [](tropical-matrix-structure.md) |
| **Row propagation on top of T1 (T2)** | 0/10 at every cap setting against the baseline's 1/10 -- a regression, not a wash. Well-controlled: the mechanism fired 2,088 times, and counters are *identical* at default/128/1024 caps because tropical never approaches even the default caps. [](qf-lra-heuristics.md) |
| **Monotone / one-sided variable elimination** | Measured with `scripts/analyze_smt2.py` across 8 QF_LRA families: **0 one-sided reals in 7 of them**, and 1 of 11 in `DTP-Scheduling`. There is nothing to eliminate. The idea is sound (Fourier-Motzkin on a single-sign variable generates no new constraints); it simply does not apply to these benchmarks. |
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
