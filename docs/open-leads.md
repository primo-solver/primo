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
- **Record what a configuration solves *uniquely*, not only how many it
  solves.** See [](#complementarity) -- a lead that loses on the promotion bar
  can still earn a place in a portfolio, and that information is destroyed by
  reporting solved counts and PAR2 alone.

## At a glance

| # | lead | expected size | cost | status |
|---|---|---|---|---|
| [1](#lead-pure-filter-lra) | Extend `--pure-theory-filter` to LRA atoms | zero as a wiring fix; the filter reaches only the once-called final check | large if done properly | **downgraded**; merge into lead 5 |
| [2](#lead-flag-first) | Flag-first decision ordering | unknown | blocked | **attempted; belongs to the theory-combination layer** |
| [2b](#lead-two-lra-paths) | Dispatch is a **nondeterminism trigger**, not a performance bug | neither path dominates (2-2 at the timeout) | -- | **measured; design kept, hazard recorded** |
| [3](#lead-leaving-rule) | MathSAT's real leaving rule: least-violated, short-row tie-break | 167 vs 174 on the bar; beats the *other* existing policy by 5 | done | **implemented, opt-in; did not clear its bar** |
| [3b](#lead-occurs-in) | `occurs_in_` removal scan | -25% where lists are long, +19% where short | small-medium | **open**; hash set rejected, 3 candidates left |
| [4](#lead-aep) | Adaptive Early Pruning | ~20% total time in its source | small-medium | open |
| [5](#lead-relevancy) | Relevancy propagation | unknown, principled | **large** | open, gated on 1 |
| [6](#lead-justification) | Justification / goal-directed decision heuristics | unknown | medium-large | open, no paper note yet |
| [7](#lead-row-representation) | Compare row representation against sorted arrays of pairs | already the endorsed design; 0.00% map cost in the profile | done | **closed: already implemented** |
| [8](#lead-dl6) | DL6: decide precision statically before search | unknown | small | blocked on a DL solver |
| [9](#lead-monotone) | Monotone (one-sided) variable elimination | neutral on the promotion bar | done | **implemented, opt-in; did not clear its bar** |
| -- | D2 floating-point-guided basis selection | ~14% in the literature | medium | open, see [](qf-lra-heuristics.md) |
| -- | D4 revised simplex, LU-factored basis | unknown | largest | open, see [](qf-lra-heuristics.md) |
| -- | H4 batched bound assertion; H6 DL fast path; H11 fill-in-aware preprocessing elimination | -- | -- | design only, see [](qf-lra-heuristics.md) |

(lead-pure-filter-lra)=
## 1. Extend `--pure-theory-filter` to LRA atoms

**Status: downgraded 2026-08-05 after reading the code. The original framing
of this entry was wrong in two ways, both recorded below.**

**What the mechanism actually is.** `should_filter_assignment` omits a theory
literal from the theory assignment when the atom occurs with only one polarity
*and the SAT solver assigned it the opposite way* -- such an assignment cannot
be what satisfies any clause, so the theory need not check its consistency
([](euf.md), from Sebastiani's survey §6.10). It omits a constraint from a
check rather than adding one, which is why it is sound where the pure-literal
rule is not ([](preprocessing.md)'s [](#pure-literal-trap)).

**Correction 1: it is not "QF_UF only".** The rule is generic -- it needs only
`occurs_positive`/`occurs_negative` -- and the LRA side already *consumes* it:
five sites skip an atom on `!prepared_atom.is_arithmetic || atom.filtered`.
What the QF_LRA path does is call the two-argument
`assigned_atoms_from_backend` overload, which hardcodes
`PureTheoryFilterMode::Off`, so `filtered` is never set. Passing
`options.pure_theory_filter` at the two call sites in `solve_qf_lra_impl` (and
one more for the LRA half of QF_UFLRA) is a two-line change. Note this is
deliberate rather than a bug: [](euf.md) documents the implemented subset as
QF_UF atoms.

**Correction 2, twice over -- the first downgrade was itself measured
wrong.** This entry cited "2,892 of 3,846 theory atoms pure-positive (75%)" as
the size of the prize, on the reasoning that a pure-positive atom is filtered
only when assigned false and most would be assigned true. A first experiment
appeared to confirm that. **That experiment was inert**: it patched
`solve_qf_lra_impl`, and these files do not use it -- `:solver.qf-uflra` is 1
and `:solver.qf-lra` is 0, so the live path is `solve_qf_uf_lra`. Identical
counters proved nothing.

Redone against the path that actually runs, the picture reverses and then
lands somewhere more useful:

| measurement on `constraint-199552` | value |
|---|---:|
| LRA atoms seen at the check | 2,892 |
| **filtered with `--pure-theory-filter=auto`** | **1,177 (41%)** |
| filtered with `=off` (control) | 0 |
| `:lra.pivots`, either way | 7,679 |
| `:lra.pivot-term-ops`, either way | 128,402,240 |

So the reasoning was wrong in the other direction too: 41% of the atoms *are*
assigned against their polarity and *are* filtered. The filter fires hard --
and changes nothing at all.

**The actual reason, and it is the useful finding.** The counters say
`:lra.theory-checks 1` against `:lra.online-check-calls 6528`. Wiring the
filter at `assigned_atoms_from_backend` reaches only the **final check, which
runs once**. Every one of the 7,679 pivots happens inside the online
propagator, which never consults `filtered`. The cheap version of this lead is
therefore worthless by construction, not by accident of the benchmark.

**What the lead becomes.** To matter, the filter has to reach the online
propagator's assertion path. That is a materially larger change than two
lines, and it sits much closer to [](#lead-relevancy) than to a wiring fix --
both are then about *not asserting* atoms during search rather than about
polarity bookkeeping at the end.

**Consequence for lead 5.** [](#lead-relevancy) was gated on this one as its
"cheap static approximation". That gating still holds, but the reasoning
inverts: this lead failing for *this* reason (nothing to filter statically)
does not say dynamic relevancy is worthless, because relevancy filters on
whether an atom is *reachable* under the current assignment, not on polarity.
The two mechanisms would fire on different atoms.

(lead-flag-first)=
## 2. Flag-first decision ordering

**Status: attempted 2026-08-05 and blocked. The stated mechanism does not
exist, and the real one is owned by a different layer.** Kept open in
principle; not actionable from the LRA propagator.

**The claim.** On guarded families the Boolean layer is self-contained and
determines which arithmetic is relevant, so settling it first should stop the
search doing tableau work a better flag assignment would have made
unnecessary. `constraint-199552` shows 15,766 decisions against only 93
conflicts -- the search is walking, not fighting.

**Why "invert the `cb_decide()` filter" is not implementable.** Measured, not
inferred:

| fact | evidence |
|---|---|
| These benchmarks do not run `solve_qf_lra` | `:solver.qf-uflra` 1, `:solver.qf-lra` 0 -- on tropical *and* uart |
| `cb_decide`'s atom list is 100% arithmetic | probe: `lra_atoms=2892 non_arithmetic=0` (tropical), `1363 / 0` (uart) |
| so inverting its `is_arithmetic` filter yields an **empty** candidate set | it would disable theory decisions, not reorder them |
| the flags are elsewhere | they are the 954 `euf_atoms` -- Bool-sorted `Constant` terms, split off by `is_euf_theory_atom` |
| the dispatcher does not help | `MixedTheoryPropagationDispatcher::cb_decide` just delegates to the LRA one, and is only built when `nelson_oppen_propagation == Online`, which is not the default |

**The hard blocker.** Passing `euf_atoms` into `OnlineLraPropagator` and
returning one of their literals from `cb_decide` aborts immediately:

```text
cadical: fatal error: invalid API usage of 'ask_decision':
external decisions are only allowed over observed variables.
```

A propagator may only decide variables it *observes*. Making the Boolean flags
observed by the LRA propagator changes which callbacks CaDiCaL delivers to it
and how much it is notified about -- that is a theory-combination decision,
not an arithmetic one.

**Design ownership, and why this was reverted rather than finished.** Deciding
*which theory's atoms get decided first* belongs to the layer that owns which
variables each theory observes -- the theory-combination/Nelson-Oppen layer,
where `MixedTheoryPropagationDispatcher` already sits and already sees both
atom sets. Bolting it into `OnlineLraPropagator` would have the LRA propagator
reach into EUF's atom list, and would collide with in-flight Nelson-Oppen work
in another branch. The prototype was reverted; nothing of it is committed.

**What would make this actionable.** A decision hook on the combination layer
that observes both theories' atoms and can order across them. That is a
natural companion to whatever the Nelson-Oppen work settles on, and should be
picked up there rather than re-attempted here.

(lead-two-lra-paths)=
## 2b. Dispatch is a nondeterminism trigger (not a performance bug)

**Status: measured and downgraded. The design is deliberate -- a dedicated
LRA-only solver without Nelson-Oppen is kept -- but the *trigger* for choosing
it is not, and that is worth knowing about.**

### What happens

`has_euf_predicate` is set by
`(kind == Application || kind == Constant) && sort == Bool`, so a bare Boolean
**Constant** -- an ordinary propositional variable -- counts as an EUF
predicate. The dispatch at `(has_equality || has_euf_predicate) &&
has_arithmetic` then sends the formula to `solve_qf_uf_lra`.

`constraint-199552` declares no sorts and no uninterpreted functions
whatsoever: it is pure QF_LRA plus propositional variables, and it takes the
mixed path anyway. **130 of 237 stratified-sample files are in that
category.** Nelson-Oppen correctly no-ops there for lack of shared real terms,
so nothing unsound or expensive happens -- but which of two solvers runs is
decided by something semantically unrelated to whether theory combination is
needed.

### It is not a performance bug -- that claim was wrong

An earlier version of this entry called the divergence a behaviour bug, on the
strength of a single instance where the mixed path was 40x faster. Measured
across all 130 files where the dispatch actually differs (same binary, one
classifier line changed, 15s cap):

| verdict | files |
|---|---:|
| comparable | **80 (62%)** |
| both timeout | 21 |
| pure path faster (1.5-7.6x) | 19 |
| mixed path faster (2.2-55.2x) | 6 |
| pure solves, mixed times out | **2** |
| mixed solves, pure times out | **2** |

**Timeout wins are exactly even, and where the paths differ the pure one is
faster more often.** Neither dominates. Large swings in both directions are
what a search perturbation does in CDCL(T) -- the 40x on tropical is one draw
from a two-tailed distribution, not evidence of a defect. Corroborating this,
`rand_80_340_1159656267_18.lp` and `0051-labyrinth-12-0` each appear as large
swings here *and* in the lead 3 comparison, on opposite sides: instances that
swing hard swing hard under any perturbation.

(dispatch-nondeterminism)=
### What to actually record: this is a nondeterminism trigger

The decision is to **keep** a dedicated LRA-only solver without Nelson-Oppen.
On a single theory the combination layer has nothing to do, and paying for it
by default would be the wrong trade. What should be written down is the
consequence:

**A semantically irrelevant edit to a formula can change which solver runs,
and therefore change runtime by an order of magnitude in either direction.**
Adding a single `(declare-fun p () Bool)` to an otherwise pure QF_LRA problem
flips the dispatch. So:

- **Benchmark results are not comparable across such an edit.** A preprocessing
  change that introduces or removes a Boolean constant can move a file between
  solvers, and the resulting timing difference will look like the
  preprocessing change's effect when it is not. This is a live hazard for
  exactly the kind of A/B this register is full of.
- **Reproduction reports should name the path.** `:solver.qf-lra` and
  `:solver.qf-uflra` distinguish them, and a performance bug report that does
  not say which one ran is under-specified.
- **It interacts with the fuzzers.** Mutation-based fuzzing (opfuzz, typefuzz)
  routinely adds and removes declarations, so a mutant and its seed can take
  different solve paths. That is fine for answer-differential testing, which
  is what those tasks do, but it means mutation timing data is not meaningful.

### Not to be "fixed" naively

Restricting `has_euf_predicate` to `Application` sends pure QF_LRA formulas to
the pure path, and on `constraint-199552` that is a 4.39s solve turning into a
180s timeout. On the full 130 files it is close to a wash. Either way it is a
coin flip dressed up as a cleanup, and it touches the dispatch that in-flight
Nelson-Oppen work also touches. If the classifier is ever tightened, it should
be for predictability, and it should be measured across the whole set rather
than justified on one instance.

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

### Outcome: implemented, did not clear the bar, and corroborated the source

Slurm job 119530, 237 files, 10s, same binary all three ways:

| leaving policy | solved | PAR2 | wrong |
|---|---:|---:|---:|
| shortest-row (current default) | **174** | **1415.97** | 0 |
| least-violated + short-row tie-break (this lead) | 167 | 1543.92 | 0 |
| most-violated, Dantzig-style | 162 | 1653.46 | 0 |

**Complementarity**, the first application of [](#complementarity): against the
default it gained 2 and lost 9; against most-violated it gained 12 and lost 7.
Virtual best of all three is 177, of default plus this rule 176, against the
default's 174.

**And the caveat in that section immediately earns its place.** Both gained
files are boundary churn, not capability: at 120s the default solves
`rand_80_340_1159656267_18.lp` in 14.73s (against 2.99s) and
`0051-labyrinth-12-0` in 10.10s (against 6.60s). So the portfolio case is
weak. The 5x on the first file is a real per-instance speedup, but "solves
what the default cannot" is not what happened.

**The result that does matter.** This rule beats the *other* existing policy
by 5 solved and 110 PAR2, gaining 12 files against it. Griggio's actual rule
really is better than the most-violated rule this project's docs had
attributed to him -- independent support, on our own benchmarks, that the
attribution corrected in [](papers/griggio-mathsat-thesis.md) was wrong.

(lead-occurs-in)=
## 3b. `occurs_in_` removal is a linear scan, and it dominates `pivot`

**Found 2026-08-06 by asking why the removal is a linear scan at all. It is
the largest measured single cost in the arithmetic layer.**

`pivot()` maintains `occurs_in_`: variable -> the list of basic variables
whose rows mention it. It is the first Markowitz factor, so it has to be
right, and `pivot()` is the only place it changes during search.

### When the erase actually happens

There are **two** removal sites, and they are unrelated in both cause and
frequency. Measured on `tropical-matrix/constraint-199552` (7,679 pivots,
128.4M merged terms):

| site | when it runs | calls | elements scanned |
|---|---|---:|---:|
| **1. dissolving the leaving row** | once per term of the leaving basic variable's old row, on **every** pivot | 264,081 | 180,921,747 |
| **2. cancellation during the merge** | only when a coefficient sums to **exactly zero** | **12,033,307** | **8,174,083,067** |
| | | | **8,355,004,814 total** |

**Site 1 is structural and unconditional.** When `basic_var` stops being
basic, its row is dissolved and rewritten in terms of the entering variable.
Every variable that appeared in that row therefore no longer has `basic_var`
among the rows mentioning it, so each gets one removal. The count is exactly
`sum over pivots of |row(leaving)|` -- 34 per pivot here. It is bounded by row
length and is *not* the problem.

**Site 2 is arithmetic and data-dependent, and it is the problem.** During the
two-way merge, a variable present in both the affected row and the scaled
entering row has its coefficients added. If they cancel to exactly zero the
variable genuinely leaves that row, so `other_basic` must leave
`occurs_in_[var]`. This fires on **9.4% of all merged terms** -- 12.0M times,
each scanning an occurrence list averaging **679** entries.

So the ratio that matters is not "removals are rare". Site 2 alone performs
**ten times more removals than there are `occurs_in` entries iterated**
(1,260,035), and its scans touch **65x more elements than there are pivot term
ops**. At a generous 4 elements/ns that is roughly **2.0s of pure scanning in
a 4.4s solve**, consistent with gprof putting `pivot` at 74% self time while
`Rational` copies sit at 1.05%.

**Why cancellation is this common here** is legible from the encoding rather
than mysterious: [](tropical-matrix-structure.md) shows the family is a DAG of
definitions over 36 free variables, so rows composed from the same handful of
`f*` variables cancel against each other systematically. Whether 9.4% is
typical of other families is **not established** -- it was measured on one
instance, and that is the first thing a prototype should check.

**The comment that hid this.** Site 2 carries the note "This is the *only*
removal the loop performs", which is true and describes *which* removal it is
-- there is no second removal inside the merge. It was read as "removals are
rare", which the measurement contradicts. Site 1 is outside that loop, which
is why the comment is not wrong, only easy to misread.

### How this was found, and why that matters

Worth recording, because the finding did not come from pursuing a lead. It
came from *closing* one, and four intermediate claims along the way were
wrong.

1. **Lead 7 was opened from a paper number** -- Griggio's thesis measures 1.20x
   for choosing hash tables over sorted arrays of pairs. Checking it closed it
   immediately: Primo already uses the sorted-array design, and item H8 went
   further than the thesis by replacing the sort with a merge.
2. **The profile taken to close it was the actual payload.** `perf` is blocked
   in this sandbox, so it is gprof over a `-pg` build. It put
   `LraTableau::pivot` at 74% self time with `Rational` arithmetic separately
   and correctly attributed (those operators are defined out-of-line in
   `rational.cpp`, so they are not folded into `pivot`).
3. **Wrong claim #1: "53.6M Rational copies is a lot".** Stated while reading
   the profile. It is 1.05% of runtime. Chasing "trivial engineering to avoid
   the copies" would have won about one percent.
4. **Correcting that pointed at the right thing.** If the copies are 1% and
   the arithmetic is separately accounted for at ~20%, then `pivot`'s 74% is
   genuinely its own loop -- comparisons, vector writes, and `occurs_in_`
   maintenance.
5. **Wrong claim #2: "removals are rare".** Taken from the comment "This is
   the *only* removal the loop performs", which says *which* removal, not how
   often.
6. **Wrong claim #3: a general principle beat the data.** Asked why not a hash
   set, the first answer was that `occurs_in(entering)` is iterated in full
   every pivot and vectors iterate better than hash sets. True, and irrelevant:
   iteration touches 1.26M entries against 8.17 billion for the removal scans.
7. **Wrong claim #4: one erase site.** The first probe instrumented only the
   cancellation branch. Asking precisely *when* the erase happens surfaced a
   second, structural site in the same function that the probe had silently
   skipped.

The transferable lesson is narrow and practical: **the profile that closes a
lead is worth reading for what else it says.** Four of the six items above are
corrections to statements made confidently from code reading, and every one of
them was settled by a counter rather than by an argument.

### Why a hash set is now the obvious question, and what the data says

The standard objection to `unordered_set` here is that `occurs_in(entering)`
is *iterated in full* on every pivot, and a vector iterates contiguously while
a hash set chases nodes. That objection is real but the magnitudes settle it:
iteration touches 1.26M entries while removal scans touch 8.17 **billion**.
Iteration is 6,500x cheaper, so almost any structure with sub-linear removal
wins even if its iteration is several times slower.

Three candidate shapes, cheapest first:

1. **Tombstones / superset invariant.** Stop removing eagerly and let
   `occurs_in_` be a conservative *superset*. The consumer already does
   `lower_bound(other_row, entering_var)`, so a stale entry is detected for
   free by checking that the found position actually holds `entering_var`,
   and skipped. Removal cost becomes zero; iteration cost grows with
   staleness, so it needs a compaction policy. Relaxes
   `verify_occurs_in_index` from equality to superset.
2. **Swap-and-pop with a position index.** Keeps contiguous iteration and
   makes removal O(1), at the cost of maintaining `position_of[var][basic]`
   through every insert and swap.
3. **`unordered_set` per variable.** Simplest to write, O(1) removal, but
   pays a node allocation per insert -- and the class comment records that
   `rows_`/`occurs_in_` were migrated *away* from `std::unordered_map`
   precisely because of "per-call heap-allocated map-node churn". That was
   the outer container rather than the inner list, so it is not the same
   change, but it is the same hazard.

### Prototype result: the hash set is a regression, and the reason is instructive

Built and measured 2026-08-06, including the initial-capacity variants.
Cancellation first: it is **not** tropical-specific -- 9.4% of merged terms
there, 13.8% latendresse, **18.0% miplib danoint-166**, 12.1% uart, 9.5% sc.
So the lead generalises.

`:lra.pivots` is **identical** between baseline and prototype on every
instance (7,679 / 3,654 / 847 / 282,354), so the hash set's different
iteration order does not perturb pivot selection: these timings measure
container cost, not a search change.

| instance | erase calls | avg list | baseline | unordered_set | change |
|---|---:|---:|---:|---:|---:|
| tropical `constraint-199552` | 12.0M | **679** | 4.24s | 3.20s | **-25%** |
| sc-21 | 1.3M | 176 | 0.48s | 0.47s | -2% |
| uart-16 | 0.35M | 86 | 0.26s | 0.25s | -4% |
| **miplib `danoint-166`** | 192.8M | **76** | 65.19s | 77.52s | **+19%** |
| latendresse ecoli | 1.2M | **43** | 0.26s | 0.34s | **+31%** |

**The deciding factor is average occurrence-list length, not total elements
scanned.** `danoint-166` scans **1.8x more** elements in total than tropical
(14.6B against 8.2B) and gets *worse*, because its lists average 76 against
tropical's 679. A `std::vector<LraVarId>` scan is contiguous, vectorisable and
cache-friendly -- very cheap per element -- while a hash erase costs a hash, a
probe and a likely cache miss whatever the length. Hashing only pays once the
list is long enough to amortise that. The crossover is somewhere between 76
and 679.

**The requested initial capacity makes it worse, not better**: on tropical,
no reserve 3.20s against `reserve(8)` 3.50s and `reserve(32)` 3.38s.
Reserving per variable pays allocation and memory for *every* variable, and
most occurrence lists are short.

**This also corrects the lead's own framing.** "8.17 billion scanned elements
must be the bottleneck" was too simple: removing the scans entirely wins 25%
on tropical, not the ~45% that a 2.0s-of-4.4s estimate implied. Contiguous
scanning is much cheaper per element than that estimate assumed.

**Where this leaves the lead.** Candidate 3 is rejected as a blanket change.
The diagnosis survives -- long occurrence lists are expensive to maintain --
and the two candidates that keep contiguous iteration are untried:
tombstone/superset, and swap-and-pop with a position index. The data also
suggests a fourth option it did not previously contain: switch representation
per variable once its list crosses a length threshold, since the two regimes
are cleanly separated by exactly that quantity.

**Falsifier.** If a prototype does not materially reduce `pivot`'s share in a
fresh profile on this instance, the scan was not where the time went and the
8.17B figure is being misread.

**Note before implementing.** This touches the core tableau invariant and
`verify_occurs_in_index`, so it needs the QF_LRA model fuzz gate on Slurm, not
just unit tests -- the same gate monotone elimination went through.

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

### Outcome: closed, already implemented

The falsifier fired. `Row` is `std::vector<std::pair<LraVarId, Rational>>`
kept ordered by var id -- the Row invariant `pivot()`'s two-way merge depends
on -- with `lower_bound` for coefficient lookup. That is the thesis's design
including its log-time-lookup trade, and the class comment records that
`rows_` *was* `std::unordered_map` and was migrated away for exactly the
reason the thesis gives. Item H8 went further than the thesis describes,
replacing the post-concatenation sort with a linear merge.

A gprof profile confirms it rather than resting on the code read
(`perf` is blocked in this sandbox; `kernel.perf_event_paranoid` is 4):

| symbol | self time |
|---|---:|
| `LraTableau::pivot` | **74.1%** |
| `Rational::operator*` | 12.0% |
| `Rational::from_int128_reduced` | 3.9% |
| `Rational::operator+` | 2.9% |
| `LraTableau::update` | 2.1% |
| `Rational` copy constructor | 1.1% |
| **hash/map in the LRA path** | **0.00%** |

The only hashtable symbols in the profile are `TermPairKey` lookups --
Nelson-Oppen interface equalities -- at 0.00%. There is no container overhead
left to win here; the thesis's 1.20x is already banked.

**What the profile points at instead**, for whoever picks up the next
arithmetic lead: term-operation volume and rational cost, not the container.
83.0M `from_int128_reduced` calls against 48.1M multiplies is roughly 1.7
normalisations per multiply, and 53.6M `Rational` copies against 1.26M row
updates is a lot of copying. Fewer terms (fill-in) or a cheaper small-rational
path are the levers this profile supports.

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

(complementarity)=
## Complementarity: losing on average is not the same as useless

The promotion bar -- solved count and PAR2 on the 237-file stratified sample
-- measures *average* quality, and that is the right gate for a **default**.
It is the wrong gate for deciding whether a technique is worth keeping at all.

Once there is a portfolio, a configuration earns its place by solving
instances the others do not, however badly it scores on the mean. This project
already reasons this way about *other solvers*: [](arithmetic.md) records that
Primo and z3 are "complementary rather than one dominating" with 30 files only
z3 solves against 11 only Primo does, and that "a portfolio of Primo and
OpenSMT would solve more than either". The same reasoning has not been applied
to Primo's **own** configurations, and it should be.

**What this changes in practice.** Every A/B between Primo configurations
should report, alongside solved count and PAR2:

- instances solved by the new configuration and **not** by the baseline
  ("gained"), and
- instances solved by the baseline and not the new one ("lost").

A lead with `gained > 0` is a portfolio candidate even when
`solved_new < solved_baseline`. A lead with `gained == 0` is dominated on the
sample and can be closed without further argument.

**Today's rejections survive the stronger criterion**, which is worth stating
so they are not reopened on this basis alone:

| lead | solved | gained | verdict under the portfolio criterion |
|---|---|---:|---|
| T2 row propagation on tropical | 0/10 vs 1/10 | **0** | dominated; closure stands |
| Monotone elimination | 173 vs 174 | **0** | dominated; closure stands |

Both were measured file-by-file rather than only in aggregate, so the
"gained" column is real rather than inferred. That is the habit to keep.

**Caveat worth stating once.** Complementarity measured on one sample at one
timeout is weak evidence -- a single uniquely-solved instance is often churn at
the timeout boundary rather than a genuine capability, which is exactly what
[](qf-lra-heuristics.md)'s D1 write-up found. Treat `gained == 1` as a
prompt to look at the instance, not as a portfolio case on its own.

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
