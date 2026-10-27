# Further model-based combination transitions

**Status: Open lead. Owner: unassigned.**

Investigate the remaining steps toward the full `DPLL(T^M)` framework of
Leonardo de Moura and Nikolaj Bjørner,
[*Model-based Theory Combination*](https://doi.org/10.1016/j.entcs.2008.04.079),
ENTCS 198(2), 2008, pp. 37–49, §4 and Appendix A. No broader transition
implementation has been selected. The
{source}`retained transition checklist <docs/papers/model-based-theory-combination.md>`
records the earlier investigation and its dated implementation stages.

## Implemented baseline

Primo already has a general component driver, model projections, reversible
equality choices, and a
{source}`mutation protocol <src/theory/theory_component.hpp#L122-L125>`.
Its production EUF/LIA adapters
currently use equality decisions rather than implementing that mutation
protocol. The dedicated Real route additionally uses scratch LRA
diversification and EUF model reconciliation. The
[combination chapter](../implementation/combination.md) describes these
implemented subsets and their acceptance checks.

## Questions to resolve

Map the paper's model state and `M-Propagate`/`M-Mutate` operations to current
component ownership and SAT callbacks. Identify which transitions the
existing interfaces already support and which need actual component
implementations. Specify invalidation after backtracking, model revision
and assignment freshness, dynamic literal creation, and progress across
repeated choices or mutations. CaDiCaL continues to own propositional search.

The paper leaves mutation scheduling open. A proposed schedule must therefore
state when models are required, when mutation is attempted, and what bounds
unproductive work. Compare it with the
[current Real schedule](../implementation/combination.md#when-model-mutation-runs).
Model coincidences remain choices; only explained consequences may become
theory lemmas.

Choose a bounded component/logic scope before implementing. Require transition
and rollback regressions, the [correctness gates](../development/correctness.md),
independent SAT-model validation, and paired measurements against the existing
route. Migrating QF_UFLRA to the general driver is a
[separate consolidation](real-combination-migration.md); completing it alone
would not establish the full paper framework.
