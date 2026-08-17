# Arithmetic Solving in Z3

## Bibliographic record

Nikolaj Bjørner and Lev Nachmanson. "Arithmetic Solving in Z3." CAV 2024,
LNCS 14681, pp. 26-41.
[DOI](https://doi.org/10.1007/978-3-031-65627-9_2).
Local copy: `docs/papers/arithmetic-reimplemented.pdf`.

Read 2026-08-17: Sections 2, 3, 6, and 8 in full; the integer and nonlinear
sections were read to delimit their relevance to Primo's QF_LRA/QF_UFLRA
fragment. This note records the paper's algorithmic content independently of
the Z3 source code. Z3 source is not an implementation source for Primo.

## Relevant scope

The paper describes the arithmetic solver introduced in Z3 4.8.8 and later
made default. Its complete waterfall also covers integer and nonlinear
arithmetic, which are outside Primo's current fragment. Four parts matter now:

1. Section 2 motivates an exact, sparse dual-simplex tableau integrated with
   CDCL and efficient backtracking.
2. Section 3.1 restates the bound-assertion dual-simplex architecture used by
   Primo's LRA solver.
3. Sections 3.2 and 3.3 describe inexpensive equality and bound propagation.
4. Section 6 states how arithmetic participates in model-based theory
   combination and how Z3 caches shared-term membership.

The paper does **not** describe primitive-integer normalization of input atoms.
Monniaux CAV 2009 Section 2.1 remains the source for Primo's early arithmetic
atom canonicalization.

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

For current convex QF_UFLRA, the paper does not reveal a missing correctness
transition. It reveals a narrower performance opportunity inside an existing
correct transition: replace or precede exact-offset row bucketing with the
current-model-filtered unit-row recognizer, retain explanations, and measure
the general `EqRows` fallback separately. This is the first implementation
experiment justified by this paper.
