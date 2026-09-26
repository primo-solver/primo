# Floating-point-guided LRA basis selection

**Status: Open lead. Owner: unassigned.**

The primary algorithm has been reviewed. Integration design, implementation,
and evaluation remain pending.

Use a floating-point linear-programming solver to suggest a useful starting
configuration for the exact arithmetic solver. The intended saving is fewer
expensive rational pivots. Whether that saving exceeds the extra work remains
an experimental question.

This carries forward the existing
{source}`investigation recorded as D2 <docs/qf-lra-heuristics.md#L2033-L2113>`. The
[LRA chapter](../implementation/lra.md) describes the current exact solver.

## Proposed mechanism and paper basis

David Monniaux, *On Using Floating-Point Computations to Help an Exact Linear
Arithmetic Decision Procedure*, CAV 2009,
[§3 and Algorithm 2](https://arxiv.org/abs/0904.3525), describes this sequence:

1. Solve an approximate version of the current equations and bounds using
   floating point. Obtain a proposed basis—the choice of basic variables—and
   which bounds the nonbasic variables sit on. The approximate answer and
   numerical assignment are not accepted as a solver result.
2. Use exact **forced pivots** to move the rational tableau toward that basis.
   Each pivot replaces a basic variable outside the target set with a
   nonbasic variable inside it, preserving the equations' solution set.
   A numerically suggested basis may be impossible in exact arithmetic;
   the operation must detect this while leaving a valid tableau.
3. Set nonbasic values using the original exact bounds, including strict-bound
   infinitesimals, and recompute basic values. Resume exact simplex to obtain
   a solution or conflict. A poor suggestion may still require a full exact
   search.

The safety argument depends on exact pivots preserving the equations and
exact checking producing all answers, explanations and model values. Preserve
the existing eventual fallback to Bland so exact search retains its termination
guarantee. A floating-point result alone cannot justify a learned
clause or a printed model.

## Integration questions

- **When to call it.**
  {source}`PreparedLraTheory <src/smt/solver_engines.cpp#L3332-L3388>` knows
  the equations, but SAT selects active bounds during search. Compare a
  root-level attempt with a bounded fallback for expensive theory checks;
  measure how much of the relevant bound set is available at each point.
- **How to install a suggestion.**
  {source}`LraTableau::pivot <src/theory/arithmetic/lra_tableau.cpp#L143-L315>`
  provides an exact pivot operation, but Primo has no target-basis driver.
  Specify assignment restoration, reverse-occurrence maintenance, violated-row
  tracking and rollback before connecting one to the live tableau. Account
  separately for the optional sum-of-infeasibilities (SOI) mode's additional state.
- **Where the cost goes.** Measure conversion, approximate solving, forced
  pivots and remaining exact repair separately. Track fill-in, coefficient
  sizes and memory: a promising basis can be expensive to reach or store.
- **Which numerical engine to use.** No dependency has been selected. Keep
  this choice within the project's MIT distribution requirements and layer
  boundaries; it must not expose SAT internals to an LP backend.

## Evidence and acceptance

The retained D2 discussion and
{source}`related-work survey <docs/papers/lra-related-work.md>` include later
studies and reasons to temper expectations. Their historical gains and
Primo profiles are not estimates for the current revision. Reprofile before
choosing an experiment. The [review record](../review.md#normalization-clarification-and-d2-lead)
identifies the primary reading; the follow-up studies still need review.

Any prototype needs tests for an unreachable target basis, strict inequalities,
misleading numerical results, partial conversion and incremental rollback.
Run the [correctness gates](../development/correctness.md), then compare total
runtime, solved count, PAR2 and memory against the same exact-only baseline on
SLURM. Measure representative easy cases as well as cases dominated by exact
arithmetic. Reject or restrict the approach if conversion and forced pivots
consume the savings, or if gains depend on a narrow set with substantial wider
regressions.
