# Complete implied-equality basis

**Status: Open lead. Owner: unassigned.**

Investigate whether computing a complete arithmetic equality basis can reduce
the cost of exact shared-pair probes. Integration design, implementation, and
evaluation remain open. This carries forward
{source}`lead 17 in the original register <docs/open-leads.md>`.

## Implemented baseline

The [combination chapter](../implementation/combination.md#explained-equality-exchange)
describes the local tableau rules and exact fallback. The
{source}`local rules <src/theory/arithmetic/lra_tableau.cpp#L1078-L1259>` can
miss entailed equalities; the
{source}`fallback <src/smt/solver_engines.cpp#L4907-L4947>` tests unresolved
model-equal pairs in both strict orientations. A complete equality basis is
not currently maintained for this purpose.

## Investigation and paper basis

Martin Bromberger, [*Decision Procedures for Linear Arithmetic*](https://publikationen.sulb.uni-saarland.de/handle/20.500.11880/28955),
PhD thesis, 2019, Chapter 5, supplies the proposed basis. Section 5.4.1 uses
the computed equality basis to normalize variables, making implied equality
a comparison of normalized expressions. Section 5.4.2 derives explanations
from the tight bounds used by that normalization. These are proposed
extensions to Primo, not a description of its current equality producer.

Start by measuring exact-probe frequency and time across representative
QF_UFLRA inputs, including the expensive tail. The retained
{source}`thesis note <docs/papers/bromberger-linear-arithmetic-thesis.md>` and
original lead preserve the earlier rationale and screening proposal.
If probes are a material cost, write an implementation checklist covering
basis construction, explanations, strict bounds, and rollback before
prototyping. Establish the supported arithmetic fragment explicitly.

Any prototype must retain the current correctness boundary, pass the
[correctness gates](../development/correctness.md), and independently validate
printed SAT models. Compare total solving time and memory with the unchanged
baseline; cheaper pair queries alone do not justify the cost of maintaining
the basis.
