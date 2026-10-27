# Fast and Flexible Difference Constraint Propagation for DPLL(T)

## Citation

Scott Cotton, Oded Maler:  
Fast and Flexible Difference Constraint Propagation for DPLL(T).

## Role in Primo

This paper is the later basis for difference-logic propagation.

## What we use later

- constraints of form `x - y <= c`
- weighted graph representation
- negative-cycle conflict detection
- propagation of implied bounds

## Initial representation

```text
x - y <= c
means
x <= y + c

Graph edge:
y -> x with weight c
```

## Not implemented initially

The initial Primo scaffold does not need a full difference-logic solver.

## Future milestones

- DL0: parse difference constraints
- DL1: graph construction
- DL2: negative-cycle detection
- DL3: conflict explanation from cycle edges
- DL4: model via potentials
- DL5: flexible propagation
- DL6: static precision analysis (see [](#dl-static-precision))

(dl-static-precision)=
## DL6 -- decide precision statically, before search

**Status: proposed, not implemented. Recorded 2026-08-04 as a near-future
item.** Source: Griggio's PhD thesis §2.7.2 (p. 72) -- see
[](griggio-mathsat-thesis.md). MathSAT applies it in its own DL-solver.

**The idea.** For difference logic solved by negative-cycle detection, you do
not need to discover at runtime whether infinite-precision arithmetic is
required. Sum the absolute values of all the constants `c` over the input
atoms `x - y <= c`; if that sum does not overflow the native integer type,
then no arithmetic the solver performs can overflow either.

**Why it is sound.** Every quantity a negative-cycle algorithm computes -- a
path weight, a cycle weight, a potential -- is a sum of a subset of the edge
weights, each used at most once on a simple path. So the sum of all `|c|` is a
static upper bound on the magnitude of any intermediate value. The check is one
pass over the atoms, done once before search.

**Why this is worth more than it first looks.** Primo already has a two-tier
rational (`util::Rational`, item H2): native small form, promoted to GMP when an
operation is *detected* to overflow. That is a **dynamic, per-operation** check
-- `grep` confirms there is no static pre-analysis anywhere in `src/`. The win
from the static version is not "avoid GMP" (the two-tier form already does that
in the common case); it is that a proven-safe instance can run the entire
negative-cycle computation in raw checked-free `int64`, with the per-operation
overflow branch removed from the hot loop altogether. That is a different and
strictly larger saving than tier selection.

**Scope, and the mistake to avoid.** This is specific to graph algorithms over
sums of input constants. It does **not** transfer to the LRA simplex tableau:
pivoting multiplies and divides coefficients, so tableau entries can grow far
beyond the sum of the input constants, and no bound of this form holds. Anyone
tempted to apply the same pre-pass to `LraTableau` should stop here. The natural
scope is DL, and by extension the UT VPI encoding into DL.

**Sequencing.** This depends on DL1/DL2 existing, so it is not actionable until
the difference-logic solver in `src/theory/arithmetic/difference_logic.hpp` is
real (item H6 Tier 2 in [](../qf-lra-heuristics.md)). Recorded now so it is not
rediscovered late, and so the DL solver is not built with a dynamic-only
precision assumption baked into its inner loop -- retrofitting an
overflow-check-free path afterwards is harder than designing for it.

**Falsifier, stated in advance.** If the static bound almost never holds on real
DL benchmarks -- i.e. the sum of `|c|` overflows on most instances -- the pass is
dead weight and the dynamic tier is all we need. The cheap way to find out is to
compute the sum over the DL-shaped families already in the benchmark set
(`DTP-Scheduling`, `clock_synchro`, `tta_startup`) and report what fraction fits
in `int64`, *before* writing any solver code that depends on it.
