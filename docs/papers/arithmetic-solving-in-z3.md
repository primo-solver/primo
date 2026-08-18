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

The follow-up `adaptive` mode is a Primo scheduling experiment around the
paper mechanism. It begins with `model-unit` and switches permanently to
`legacy` when shared-model reconciliation first has to materialize a SAT
equality split. That event means the cheap entailed equalities were
insufficient to reconcile the current theory models; it is therefore a
semantic escalation trigger rather than a tuned numeric threshold. The switch
is permanent within one `check-sat` call because later SAT assignments can
expose additional fixed-row equalities. Counters distinguish unit rounds,
legacy rounds, and the single fallback activation. This adaptive policy is not
from the paper and remains experimental until its own fuzz and corpus A/B are
complete.

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
correct transition: replace exact-offset row bucketing with the
current-model-filtered unit-row recognizer, retain explanations, and leave the
strict-orientation oracle as the completeness path. The opt-in `model-unit`
producer is the completed experiment. It substantially reduces producer-local
work, but the recorded full-corpus loss rejects default promotion. A future
adaptive design would need to recover useful general `EqRows` lemmas without
paying their exact-offset cost on every round.
