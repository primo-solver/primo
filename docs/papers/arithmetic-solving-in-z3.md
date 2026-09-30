# Arithmetic Solving in Z3

## Bibliographic record

Publication: {cite:p}`BjornerN24`. Section references use the CAV 2024 edition.

**Source refresh, 2026-09-25:** §§2–3 and 6 were reread from the supplied PDF
for the replacement LRA chapter. The chapter retains direct/indirect propagation
and the optional §3.2 model-unit producer, including its unpromoted status.
The implementation and experiment trail below remains available for the
integer and combination chapters; this refresh does not rerun its evaluations.

Read 2026-08-17: Sections 2, 3, 6, and 8 in full; the integer and nonlinear
sections were read only to delimit their relevance to what was then Primo's
QF_LRA/QF_UFLRA-only fragment.

**Re-read 2026-08-21, Section 4 in full.** The original reading deliberately
skipped the integer section because QF_LIA did not exist in Primo yet. It does
now, with four stages shipped, so Section 4 is the directly relevant part of
this paper and is summarized below. Sections 5 (non-linear) and 7 (evaluation)
remain out of scope; Section 7's headline numbers are noted only where they
calibrate expectations.

This note records the paper's algorithmic content independently of the Z3
source code. Z3 source is not an implementation source for Primo.

## Relevant scope

The paper describes the arithmetic solver introduced in Z3 4.8.8 and later
made default. Its complete waterfall also covers non-linear arithmetic, which
is outside Primo's current fragment. The parts that matter:

1. Section 2 motivates an exact, sparse dual-simplex tableau integrated with
   CDCL and efficient backtracking.
2. Section 3.1 restates the bound-assertion dual-simplex architecture used by
   Primo's LRA solver.
3. Sections 3.2 and 3.3 describe inexpensive equality and bound propagation.
4. **Section 4 is the integer waterfall Primo's QF_LIA path is a partial
   implementation of** -- see the dedicated section below.
5. Section 6 states how arithmetic participates in model-based theory
   combination and how Z3 caches shared-term membership.

The paper does **not** describe primitive-integer normalization of input atoms.
Monniaux CAV 2009 Section 2.1 remains the source for Primo's early arithmetic
atom canonicalization.

## Section 4: the integer waterfall

The paper states the order explicitly: first *patch* real solutions into
integer ones; if patching leaves integer variables fractional, check
light-weight Diophantine infeasibility criteria; then resort to Gomory cuts and
branch-and-bound. Primo implemented the third and fourth layers first; patching
(4.1) followed on 2026-08-21, leaving cubes (4.2) as the one absent layer.

### 4.1 Patching -- IMPLEMENTED 2026-08-21 (stage 5)

The observation is that in a feasible tableau with all non-basic variables at
their bounds, integer-sorted non-basics already hold integer values, so *only
basic variables* can be fractional. Patching changes non-basic values to make
basic ones integral: for a row with basic `xb`, find a `delta` of minimal
absolute value such that moving one non-basic `y` by `delta` stays within `y`'s
bounds, makes `xb` integral, and breaks no other bound.

The paper's example: `y - x/2 = 0`, `z - x/3 = 0`, with `x` in `[3,10]`, `y` in
`[-3,4]`, `z` in `[-5,12]`. Starting at `x = 3` gives `y = 3/2`. Moving `x`
within its freedom interval `[3,8]` to `x = 6` makes both `y = 3` and `z = 2`
integral.

The paper explicitly reports a **negative** result worth recording: the related
method of diversifying values via freedom intervals (their citation [18]) "does
not preserve integral assignments". So freedom intervals bound the search space
for `delta`, but the diversification heuristic itself is not the technique.

**Two things the paper's description leaves implicit**, both found by building
Example 3 as a test rather than trusting the prose. First, "|delta| is minimal"
cannot be read per-variable: the nearest fix for `y` moves `x` to 4 and breaks
`z`, and the nearest fix for `z` then moves it straight back, so a naive
implementation oscillates. Candidates have to be scored by their effect on
*all* integer variables. Second, the two integers bracketing the current value
are not a sufficient candidate set -- Example 3's own answer `x = 6`
corresponds to `y = 3`, two integers above `y`'s current `3/2`, so floor/ceil
alone never proposes it. See [](../qf-lia-branch-and-bound.md)'s "Patching".

### 4.2 Cubes -- not implemented in Primo

Bromberger and Weidenbach's test: `Ax <= b` is integer feasible if
`Ax <= b - (1/2)||A||_1` has a solution over the **reals**, where `||A||_1`
taken as a column vector has entry `i` equal to the sum of absolute values in
row `i`. So one real-feasibility check on a uniformly tightened system decides
integer feasibility positively, and the real solution rounds to an integer one.

The paper's example: for `3x + y <= 9 AND -3y <= -2`, solve
`3x + y <= 9 - (1/2)(3+1) = 7` and `-3y <= -2 - (1/2)3 = -3.5`; a real model
`y = 7/6, x = 0` rounds to an integer solution of the original.

Z3's refinement over the original method: difference inequalities `x - y <= k`
with integer `k` never need strengthening (real-feasible iff integer-feasible),
and octagon constraints `±x ±y <= k` need it only when the two variables sit at
midpoints between integral solutions.

The paper calls this "an important factor in solving more **satisfiable**
integer arithmetic instances" -- which is exactly where Primo's measured
deficit is (see the gap analysis below).

**Read against the primary source, 2026-08-29.** Bromberger's thesis
([](bromberger-linear-arithmetic-thesis.md), Chapters 4--5) is the origin of
this test, and it changes two things this section implied.

- The precondition that blocks Primo (an asserted equality fixes a slack, so the
  tightened system is contradictory) is a **named, cheap skip condition** in the
  original, not a defect: skip when an integer variable has `U - L = 1` or a
  slack has `U - L = 0`, and substitute out fixed integer variables first
  (§7.2.4). z3's difference/octagon twist does not address it and is therefore
  *not* the prerequisite [](../open-leads.md) lead 12c recorded.
- The actual prerequisite for equality-heavy inputs is the **implied-equality
  basis** of the thesis's Chapter 5, which exists precisely because equalities
  break the cube tests; §5.7 lists equality elimination as "useful as a
  preprocessing step for our cube tests".

Size, from the thesis's own ablation on the full QF_LIA SMT-LIB division:
6806 with the unit cube test vs. 6750 without (+56), over a population of
**1483 absolutely unbounded instances**, with the test removing the need for
branch-and-bound entirely on 1271 of them.

### 4.3 GCD consistency -- Primo has the basic test, not the extended one

The basic test is the one Primo's stage 2 implements: fold fixed variables into
a constant offset and check that the gcd of the remaining coefficients divides
it.

The paper then extends it in a way Primo does not have. For a row
`a.x + b.y + c = 0` where the coefficients in `a` are all equal and smaller
than those in `b`, and **the variables `x` are bounded but not necessarily
fixed**, let `l := a.lb(x)` and `u := a.ub(x)` be the achievable range of the
bounded part. If that range contains no value congruent to the required residue
modulo `gcd(b, c)`, there is no solution.

**Caution on the printed inequality.** The paper renders the condition as
`floor(u/gcd) > ceil(l/gcd)`, which is backwards as written -- that direction
would report infeasibility precisely when a solution exists. The intended test
is `floor(u/gcd) < ceil(l/gcd)`, i.e. no multiple of the gcd lies in `[l, u]`.
Checked against the paper's own shape with `l = 2, u = 3, g = 4`:
`floor(3/4) = 0 < ceil(2/4) = 1`, correctly infeasible. Anyone implementing
this should not transcribe the printed direction.

This extension matters concretely: it closes the exact case
`docs/qf-lia-branch-and-bound.md` documents as caught by neither stage 2 nor
stage 3 -- `4x + 4y` bounded to `[2,3]` with `x, y` unbounded, which currently
diverges to `--lia-branch-limit` and is pinned by
`gcd_uncatchable_divergent_system_hits_the_node_limit` in
`tests/unit/qf_lia_solver_tests.cpp`. Primo's stage 2 misses it because
`fixed_value()` requires equal lower and upper bounds, and here the slack is
bounded but not fixed.

### 4.4 Branching -- Primo's is theory-internal, Z3's is a SAT literal

Z3 "creates somewhat eagerly case splits on bounds of integer variables"; for a
variable assigned `1/2` it creates *a literal* `x <= 0` that triggers the two
branches. That is CDCL-integrated branching, so conflict learning generalizes
across branches. Primo's `branch_and_bound` runs an explicit-stack DFS entirely
inside the theory with synthetic `kDerivedBoundReason` bounds -- a deliberate
scoping decision recorded in `docs/qf-lia-branch-and-bound.md`'s "Where
branching lives", not an oversight.

### 4.5 Cuts -- Primo has the base method, none of the enhancements

Z3 implements Chvatal-Gomory cuts plus cuts derived after transforming the
linear system into Hermite normal form. Primo implements one mixed Gomory cut
(King's thesis eq. 4.9). The paper adds five things Primo lacks:

1. **A relaxed Gomory-row definition.** The strict definition requires every
   non-basic `xj` in the row to sit at a non-strict bound. Z3 relaxes this: a
   non-basic *integral* `xj` may be away from its bounds provided `beta(xj)`
   and `aj` are both integers -- such `xj` is called **row integral**. This
   directly widens eligibility, which is Primo's measured failure mode (the
   instrumented `f_unbounded` count).
2. **Explanations must track the relaxation.** The paper notes the Gomory
   term's explanation does *not* include constraints on the relaxed
   (row-integral) variables -- but that in the extremum case below, those
   constraints *do* have to be added. This is a direct constraint on Primo's
   new `LiaExplanation` antecedent rule if the relaxation is adopted.
3. **A cut-selection heuristic.** Sort Gomory rows by the distance of
   `beta(xb)` from the nearest integer and take a few with *minimal* distance,
   breaking ties toward variables occurring in more terms. The paper notes
   earlier heuristics used distance to bounds instead. Primo takes the first
   eligible row.
4. **Extremum detection.** If every `xj` is at its lower bound and every
   `aj > 0`, then `xb` is at its maximum and one can deduce `xb <= floor(beta(xb))`
   directly -- a bound, not a cut.
5. **Large-numeral discipline.** Cuts with large coefficients are added to a
   temporary scope first and only re-added to the main scope if the temporary
   tableau is infeasible; bounds propagation is likewise skipped when it would
   need big-num arithmetic. Primo uses `--lia-gomory-cut-limit` as a blunt
   substitute.

The paper also states plainly that "cuts are consequences of the current
bounds", which is the same justification Primo's stage 4 explanation rests on.

### What Section 7 says about expectations

On QF_LIA the paper reports z3 solving 6,276 and CVC5 3,556 of the same set at
600 s, with MathSat5 at 6,368 and Yices2 at 6,035. The relevant lesson for
Primo is the paper's own summary of its ablation: apart from incremental
linearization (a non-linear feature), "other features have each a relative
minor effect in isolation. The solver relies on their cumulative effect." A
single missing layer should not be expected to close a large gap on its own.

## Gap analysis against Primo (2026-08-21)

Primo's own full-corpus measurement points at the same place the paper does.
Over all 13,306 QF_LIA files at 60 s (Slurm 127949 and 127709):

| solver | sat | unsat | total |
| --- | --- | --- | --- |
| primo | 5,866 | 2,859 | 8,725 |
| cvc5 | 6,527 | 2,712 | 9,239 |
| z3 | 7,825 | 4,322 | 12,147 |

Primo is **ahead of cvc5 on unsat by 147** and **behind on sat by 661**. The
entire deficit to cvc5 is on satisfiable instances. That is a precise match for
what is missing: sections 4.1 (patching) and 4.2 (cubes) are the two layers
aimed squarely at *finding* integer models, and Primo implements neither, while
the layers Primo does implement -- GCD consistency, Gomory cuts,
branch-and-bound, and now conflict generalization -- are all refutation
machinery.

### Complete audit of what Primo has and lacks (2026-08-22)

| paper | technique | status |
| --- | --- | --- |
| 3.1 | dual simplex, bound assertion | **have** |
| 3.2 | cheap equal-variable discovery | **N/A for QF_LIA** -- the paper states equality propagation is disabled for pure arithmetic theories, QF_LIA and QF_LRA included. Relevant only to QF_UFLRA, where Primo now has both: the legacy exact producer (default) and the 2024 fast path behind `--qf-uflra-lra-equality-producer=model-unit`, measured and not promoted |
| 3.3 | direct bounds propagation | **have**, on by default |
| 3.3 | **indirect (row-based) bounds propagation** | **implemented but off by default -- and now measured on QF_LIA: it does not help** (see below) |
| 4.1 | patching | **have** (stage 5, +300 solves) |
| 4.2 | cube test | **missing**, deferred behind z3's difference/octagon twist |
| 4.3 | GCD consistency, basic | **have** (stage 2) |
| 4.3 | GCD consistency, extended to bounded-not-fixed rows | **missing** (lead 12e) |
| 4.4 | branching on integer bounds | **have**, but theory-internal DFS |
| 4.4 | **branching as CDCL literals** | **missing** -- z3 creates a real SAT literal so conflict analysis generalizes across branches; Sect. 2 names this eager-case-split design as deliberate |
| 4.5 | Gomory cuts | **have** (stage 3, King's mixed cut) |
| 4.5 | relaxed "row-integral" Gomory rows | **missing**; has measured evidence behind it (the instrumented `f_unbounded` count) |
| 4.5 | cut selection by distance to nearest integer | **missing**; Primo takes the first eligible row |
| 4.5 | extremum bound deduction | **missing** |
| 4.5 | Hermite-normal-form cuts | **missing**, large |
| 4.5 | large-numeral temporary scope | **missing**; `--lia-gomory-cut-limit` is the blunt substitute |

### Row-based propagation does not transfer to QF_LIA

Sect. 3.3's indirect propagation is the one paper technique Primo already
implements and simply leaves off (`lra_row_propagation = false`). It had been
fuzz-gated and A/B-tested repeatedly -- 17 run-log entries -- but **every one of
those was QF_LRA or QF_UFLRA, and none was QF_LIA**, which is now the fragment
where profiling says the bottleneck lives.

Measured on the per-family sample (94 files, 10 s), including the exact widened
caps `sandbox/paper.tex`'s RamParILS tuning selected for its `primo+`
configuration (`max-row-size 256`, `max-fanout 1024`, against defaults of 64):

| arm | solved | sat | unsat | wrong | PAR2 |
| --- | --- | --- | --- | --- | --- |
| default | **58** | 30 | 28 | 0 | **7.83** |
| row propagation, default caps | 57 | 29 | 28 | 0 | 8.09 |
| row + bidirectional | 58 | 30 | 28 | 0 | 7.94 |
| row propagation, paper caps | 57 | 29 | 28 | 0 | 8.12 |
| `--lra-model-phase` | 55 | 27 | 28 | 0 | 8.42 |
| both (the paper's `primo+`) | 56 | 28 | 28 | 0 | 8.37 |

**Zero gains in any arm.** Individual losses of one to three could be sample
noise, but the absence of a single gain across six configurations is not. The
QF_LRA tuning does not transfer: `lra_row_propagation` and `lra_model_phase`
are both worth keeping off for QF_LIA, and that is now measured rather than
inherited. This is also a result for the paper itself -- its tuned
configuration is fragment-specific, not a better default.

### What is worth attempting next, and the caveat over all of it

Ranked by expected value for Primo:

## Section 3.2: cheap equal-variable discovery

The goal is to propagate equalities between shared arithmetic variables without
subtracting large rows or enumerating every shared pair. The paper first limits
attention to variables equal in the current arithmetic assignment. Model
equality is only a candidate condition; the tableau shape and fixed variables
establish entailment.

The reported useful shapes are:

- a shared basic variable `x` has a row `x - y + alpha = 0`, where `y` is
  shared and every variable in `alpha` is fixed;
- two shared basic variables `x` and `y` have rows connected through the same
  non-basic `z`, with the same unit sign on `z`, while all remaining terms in
  both rows are fixed.

Because the current assignment already has `beta(x) = beta(y)`, feasibility of
the rows establishes that the fixed remainders agree. The implementation need
not construct or hash their potentially large exact numeric offsets. The paper
reports that Z3's older offset-equality table was costly specifically because
computing those offsets can involve large numerals and because the hash-table
operations add overhead. It also reports no measurable gain from generalizing
the connecting variable beyond unit coefficients.

### Mapping to Primo

`LraTableau::model_equal_classes()` already provides the required candidate
filter over exact `DeltaRational` values. `LraTableau::entailed_equalities()`
implements the sound `FixedEq`, `EqRow`, `EqOffsetRows`, and general `EqRows`
rules from de Moura and Bjørner 2008, with explanations.

The default `legacy` producer is not the 2024 fast path:

- `EqRow` computes the exact value of the fixed remainder;
- `EqOffsetRows` constructs and hashes an exact fixed offset;
- `EqRows` constructs and hashes the complete non-fixed polynomial plus exact
  fixed offset;
- the producer scans all supplied shared variables before the model-equality
  partition is used as a narrow candidate frontier.

These operations are sound, but they include the offset arithmetic and hashing
that the paper identifies as avoidable. The general `EqRows` rule may still be
useful; the paper's negative result for non-unit generalization makes it an A/B
question rather than a reason to delete it without measurement.

### Implementation checklist and experimental producer

The `model-unit` producer implements this mechanism behind
`--qf-uflra-lra-equality-producer=model-unit`. The completed differential
fuzzing and full-corpus A/B do not justify replacing the default `legacy`
producer; the measured tradeoff is recorded below. The implementation
obligations are:

1. Build the exact current-model equality classes once and inspect only
   non-singleton shared classes.
2. Recognize the one-row and same-sign two-row unit-coefficient shapes without
   constructing fixed offsets.
3. Collect every fixing lower/upper bound used by the matched rows so the
   published equality retains a checkable explanation.
4. Emit a deterministic spanning set, not all pairs in a model class.
5. Keep the existing exact producer or strict-orientation fallback as the
   completeness path; failure to match a cheap shape is not disequality.
6. Preserve the distinction between model-equal conjectures and entailed
   equalities.

Expected work is linear in the shared variables and nonzeros of candidate rows,
plus emitted explanation literals. Required counters are candidate model
classes and pairs, one-row matches, two-row matches, rows/nonzeros inspected,
and fallthrough to offset/general/exact producers. Tests need large fixed
numerals so they exercise avoidance of offset arithmetic, both signs of the
connecting unit variable, non-unit rejection, transitive spanning output, and
explanation invalidation when any fixing bound is removed.

### Detailed implementation contract

The fast producer is a read-only query on a feasible `LraTableau`. It receives
only the LRA variables corresponding to the solve-scoped shared Real interface;
pure QF_LRA never calls it. It first partitions those variables by their exact
`DeltaRational` assignment. Singleton classes cannot produce a model-guided
equality and are discarded. A variable-to-class map then lets the row scan
reject candidates from different model classes without arithmetic.

For a shared basic variable `x`, write its stored row as `x = sum a_i x_i`.
The one-row rule accepts a shared `y` in the same model class when `a_y = 1`
and every other row variable is fixed. Since `beta(x) = beta(y)`, the fixed
remainder evaluates to zero in the current model; because every variable in
that remainder is fixed, it evaluates to zero in every model satisfying the
current bounds. Therefore `x = y` is entailed without constructing the
remainder's value.

The two-row rule records a shared basic variable when its row has exactly one
non-fixed variable `z`, whose coefficient is `1` or `-1`; every other row
variable must be fixed. Two records in the same model class with the same `z`
and coefficient have equal fixed remainders, again because their basic
variables have equal current values. Their equality is therefore entailed.
Opposite signs, non-unit coefficients, or another non-fixed term do not match.

Each fixing premise contributes both bound reasons. Reasons are sorted and
deduplicated before publication. A deterministic disjoint-set structure over
each model class emits an edge only when it joins two previously disconnected
components, bounding output by `n - 1` equalities per class. Missing a match is
not a negative result: the mixed coordinator retains its complete
strict-orientation fallback for unresolved pairs.

With expected constant-time hash-table operations, the structural scan costs
`O(S + N)`, where `S` is the number of supplied shared variables and `N` is the
number of nonzeros in rows of candidate shared basic variables. If emitted
equality `i` has `r_i` explanation literals, sorting and deduplication adds
`O(sum_i r_i log r_i)`. The query must not pivot, add rows, assert bounds,
create terms, or compute/hash fixed numeric offsets. The legacy producer
remains available and is the default.
Counters under `:no.interface.lra-model-unit-*` expose classes, candidate
pairs, rows, nonzeros, matches, rejections, and reasons;
`:time.no-lra-equality-producer-us` records aggregate query time.

### Evaluation and default decision

Commit `5e9371a` was tested on SLURM with the option enabled. Job `127110`
passed 3,000 generated mixed QF_UFLRA cases, 1,000 finite-domain cases, and
3,500 model checks; 1,947 SAT models passed Dolmen. Job `127111` then checked
all 11 tracked QF_UFLRA seeds and completed 43,936 YinYang solver calls with no
bug trigger. The full 1,284-file, 120-second A/B used jobs `127115` (`legacy`)
and `127116` (`model-unit`). Both had zero wrong answers, errors, or unsupported
rows.

The fast producer reduced aggregate producer time from 2,579,190 to 698,229
microseconds and explanation literals from 26,234,347 to 7,116,559. On the
1,271 common solves, aggregate wall time improved from 760.05 to 740.33
seconds. However, `legacy` solved 1,272 files and `model-unit` solved 1,271. The
lost CPAchecker induction row was SAT in 77.12 seconds under `legacy` and timed
out at 120 seconds under `model-unit`. On that row the legacy producer found
2,226 general `EqRows` matches that the unit-only producer deliberately does
not implement; the candidate delivered fewer opportunistic equalities and did
not invoke its strict-orientation fallback for those pairs.

This is a search-strength regression, not a correctness failure. The result
validates the paper's local cost observation, but rejects replacing Primo's
broader producer wholesale. `legacy` therefore remains the default.

Three exact-commit repetitions made the loss reproducible rather than a
120-second boundary effect. At commit `fcc8d0e`, `legacy` solved the row in
70.62--70.67 seconds, while `model-unit` needed 180.53--182.60 seconds. Enabling
the candidate final scan produced the same search statistics and
180.82--182.34-second times. The missing early lemmas, rather than the final
completeness check, are decisive: `legacy` reaches a fixed point in three
convex rounds after 2,226 general `EqRows` matches, whereas `model-unit` takes
33 rounds, 22 shared-model splits, 86 assumption solve calls, and 842 equality
assumptions. Its solve-phase time increases by about 112 seconds even though
the producer itself remains cheap.

The follow-up `adaptive` mode is a Primo scheduling experiment around the
paper mechanism. It runs `legacy` until an opportunistic producer round causes
no SAT recheck, uses `model-unit` on later rounds, and returns permanently to
`legacy` when shared-model reconciliation still has to materialize a SAT
equality split after a model-unit round. A split reached directly from the
bootstrap instead starts the cheap phase; model-unit has not failed yet. This
propagation-fixed-point bootstrap preserves general `EqRow` clauses that
actively change the initial SAT search trajectory. A later split means the
cheap entailed equalities were
insufficient to reconcile the current theory models; it is therefore a
semantic escalation trigger rather than a tuned numeric threshold. The
fallback is permanent within one `check-sat` call because later SAT assignments
can expose additional fixed-row equalities. Counters distinguish the bootstrap,
unit rounds, legacy rounds, and the single fallback activation. This adaptive
policy is not from the paper.

The completed evaluation at commit `d8fc162` shows that this scheduling rule
repairs the regression conservatively. The lost row solved three times in
73.65--74.02 seconds with exactly the legacy decision and propagation counts.
The fresh gate passed 3,000 mixed cases, 500 finite QF_UF cases, 500 finite
QF_UFLRA cases, and 3,500 model checks; all 1,991 SAT models passed Dolmen. On
the productive mutation replay, all 11 tracked seeds were valid and 43,854
solver calls found no bug trigger. On the complete 1,284-file corpus, adaptive
and legacy both solved 1,272 files with the same 12 timeouts and no wrong
answers. Adaptive entered `model-unit` on 77 files (93 rounds) and activated the
permanent fallback on 40 files.

The result does not justify changing the default. Aggregate producer time fell
only from 2,579,190 to 2,459,320 microseconds (4.6 percent), and 67 of the 77
files that entered the cheap phase retained identical SAT/combination search
statistics. The old and new full runs were not concurrent; their 52.57-second
wall-time difference is therefore not attributable to the mechanism, especially
because an earlier adaptive version that selected `model-unit` zero times showed
a similar cluster-time shift. `adaptive` remains an explicit experimental
profile, while `legacy` remains the default.

## Section 3.3: bounds propagation

The paper separates direct from indirect propagation. Direct propagation uses
an index from an arithmetic variable to existing bound atoms, so asserting a
stronger bound can immediately imply weaker visible bounds. Indirect
propagation inspects updated tableau rows for bounds stronger than those already
asserted, then routes them through the same index.

Primo has direct bound sharing and optional row-bound propagation, but this
paper is a useful independent source for their architecture and scheduling.
It does not justify enabling broader row propagation by default: the existing
QF_UFLRA A/B exchanged one solve for one loss, so default policy remains an
empirical decision.

## Section 6: shared equalities

For arithmetic combined with another theory, the paper requires a satisfying
state to assign the equality literal `x = y` true whenever shared terms have
the same arithmetic model value. This is model-based combination, citing de
Moura and Bjørner 2008; it is not permission to propagate accidental model
equality as an arithmetic consequence.

The paper also reports that repeatedly discovering whether a term occurs in a
shared context is expensive. Z3 caches sharedness in its E-nodes and invalidates
the cache when parent links or congruence classes change.

Primo's specialized QF_UFLRA path computes `shared_real_terms` once after
clausification and stores the resulting immutable interface in
`TheoryCombinationManager` for the solve. It therefore already obtains the
same lifecycle benefit without a mutable E-node cache. A future generic or
dynamically growing combination interface must preserve that property rather
than rediscover sharedness by repeated parent traversal.

## Resulting priority

**For QF_LRA/QF_UFLRA** (the 2026-08-18 producer experiments): for current
convex QF_UFLRA the paper does not reveal a missing correctness
transition. It reveals a narrower performance opportunity inside an existing
correct transition: replace exact-offset row bucketing with the
current-model-filtered unit-row recognizer, retain explanations, and leave the
strict-orientation oracle as the completeness path. The opt-in `model-unit`
producer is the completed experiment. It substantially reduces producer-local
work, but the recorded full-corpus loss rejects default promotion. The
experimental `adaptive` policy tests whether a propagation-fixed-point legacy
bootstrap can recover useful general `EqRows` lemmas without paying their
exact-offset cost on every later round. It recovers the lost row and preserves
the full solved set, but its small producer-local saving does not justify
changing the default.

**For QF_LIA** (the 2026-08-21 re-reading): the paper reveals two entire
missing *layers*, not a tuning opportunity. Primo's integer waterfall started
at the paper's third layer. Patching (4.1) and cubes (4.2) both exist to find
integer models, and Primo's measured weakness is precisely and only on
satisfiable instances.

**Patching shipped the same day** as stage 5 -- see
[](../qf-lia-branch-and-bound.md)'s "Patching" section. Cubes was considered
first (cheaper, no tableau change) and rejected on a measurement: 249 of 300
sampled instances contain equality atoms, and an asserted equality fixes a
slack, which makes the tightened cube system contradictory. Cubes would be
inconclusive on most rounds without also building z3's difference/octagon
twist, so it stays deferred behind that twist rather than as a standalone
item. The extended GCD test (4.3) is the smaller, well-scoped next candidate,
and closes an already-documented divergence.

Both should be measured against the same-binary control the stage 4 evaluation
established, and neither should be expected to close the cvc5 gap alone -- the
paper's own ablation reports that its features matter cumulatively rather than
individually.
