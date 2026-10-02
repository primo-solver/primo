# Move QF_UFLRA to the general component driver

**Status: Open lead. Owner: unassigned.**

Consolidate the dedicated Real route onto the existing general component
driver. Integration planning and migration remain open. The general driver
is already implemented and used by QF_UFLIA and quantified UF; this proposal
concerns bringing QF_UFLRA onto that driver.

## Implemented baseline

The [combination chapter](../implementation/combination.md) describes both
routes. {source}`solve_component_recipe <src/smt/component_solver.cpp>` owns
the general SAT loop and component lifecycle, configured by
{source}`CombinationRecipe <src/smt/component_solver.hpp#L19-L45>`.
QF_UFLRA retains its
{source}`dedicated engine <src/smt/solver_engines.cpp#L10801-L10847>`.
The {source}`completed component migration record <docs/n-way-combination-migration.md>`
preserves the earlier scope and qualification; that work deliberately left
the Real route in place.

## Scope and first review

Map the Real engine's responsibilities to the existing component contracts:
explained equality exchange, internal model reconciliation, scratch mutation,
explanation lifetimes, model construction, and exact fallback. Preserve
typed inconclusive results, shared assignment ownership, and current options.
Record any contract gaps before choosing an implementation sequence.

Use the dedicated route as the behavioral and performance baseline. Reuse
the chapter's invariant-to-test map, then run fresh
[correctness gates](../development/correctness.md), independent model
validation, and paired evaluations before switching production dispatch.
The old qualification record does not qualify a future migration.

This is an architectural consolidation. The
[further model-based transition investigation](model-based-combination.md)
and [care-guided combination investigation](care-guided-combination.md)
have separate algorithmic and correctness obligations.
