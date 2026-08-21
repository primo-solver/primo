# Arithmetic Solving in Z3

## Bibliographic record

Nikolaj Bjørner and Lev Nachmanson. "Arithmetic Solving in Z3." CAV 2024,
LNCS 14681, pp. 26-41.
[DOI](https://doi.org/10.1007/978-3-031-65627-9_2).
Local copy: `docs/papers/arithmetic-reimplemented.pdf`.

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
branch-and-bound. Primo implements the third and fourth layers and none of the
first two.

### 4.1 Patching -- not implemented in Primo

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

Ranked by expected value for Primo:

| # | Missing piece | Paper | Targets | Notes |
| --- | --- | --- | --- | --- |
| 1 | Patching | 4.1 | sat | `LraTableau::freedom_interval()` already exists; bounds the `delta` search |
| 2 | Cube test | 4.2 | sat | Cheapest of the two; one tightened real check. Already named as deferred in Primo's docs |
| 3 | Extended GCD (bounded, not fixed) | 4.3 | unsat | Closes a documented, test-pinned divergence; beware the printed inequality direction |
| 4 | Relaxed (row-integral) Gomory rows | 4.5 | both | Addresses Primo's measured eligibility failures; changes explanation rules |
| 5 | Cut selection by distance to nearest integer | 4.5 | both | Primo takes the first eligible row |
| 6 | Extremum bound deduction | 4.5 | both | Cheap, and the explanation caveat is stated |
| 7 | HNF cuts | 4.5 | unsat | A separate cut family, large |
| 8 | Large-numeral temporary scope | 4.5 | robustness | Primo substitutes a blunt cut limit |
| 9 | CDCL-integrated branch literals | 4.4 | both | Deliberately out of scope so far |

One caveat that this paper does not address and Primo's own measurement does:
an instrumented run found that 24 of 36 sampled QF_LIA timeouts never execute a
single integer round at all. For those files the bottleneck is the Boolean and
LRA layers, so nothing in Section 4 can reach them. Section 3.3's indirect
bounds propagation is the part of the paper aimed at that population.

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

The implementation is not yet the 2024 fast path:

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

### Implementation checklist for a future fast producer

This paper mechanism is **not implemented yet**. An implementation must:

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

**For QF_LRA/QF_UFLRA** (the 2026-08-17 reading's conclusion, unchanged): the
paper reveals no missing correctness transition, only a narrower performance
opportunity inside an existing correct transition -- replace or precede
exact-offset row bucketing with the current-model-filtered unit-row recognizer,
retain explanations, and measure the general `EqRows` fallback separately.

**For QF_LIA** (the 2026-08-21 re-reading): the paper reveals two entire
missing *layers*, not a tuning opportunity. Primo's integer waterfall starts at
the paper's third layer. Patching (4.1) and cubes (4.2) both exist to find
integer models, and Primo's measured weakness is precisely and only on
satisfiable instances. Cubes is the cheaper of the two to implement and is
self-contained (tighten every bound by half the row 1-norm, run one real
check), so it is the first experiment this re-reading justifies. The extended
GCD test (4.3) is a smaller, well-scoped second candidate that closes an
already-documented divergence.

Both should be measured against the same-binary control the stage 4 evaluation
established, and neither should be expected to close the cvc5 gap alone -- the
paper's own ablation reports that its features matter cumulatively rather than
individually.
