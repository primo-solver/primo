# Equation elimination across queries

**Status: open; substitution and restoration design required. Owner: unassigned.**

Whole-query equation elimination is incompatible with current assumptions
sessions. If an assertion `x = y + 1` removes `x`, a later assertion about `x`
needs the substitution or a restoration step; popping the equality must also
restore the correct context. A model conversion only reconstructs an answer
and does not transform future assertions.

Bjørner and Fazekas, *Incremental Pre-processing for SMT*, §3.2 and §5
{cite:p}`bjorner2023incremental`, provides the replay/restoration distinction.
Primo does not implement that mechanism. The [session chapter](../implementation/sessions.md)
documents its conservative current restrictions; the
{source}`assumptions admission and rewrite pipeline <src/smt/smt_session.cpp#L101-L155>`
rejects assertion-dependent options and does not run whole-goal equation elimination.

Specify scope/identity tracking, future assertions, guarded consequences,
redeclarations, cancellation and saved model snapshots before changing the
pipeline. Assertion-dependent substitutions must not become unconditional
definitions. Add tests for append/pop/restore sequences and counters for
eliminated/restored variables and associated work.

{source}`Lead 24 <docs/open-leads.md#L2792-L2817>` retains the historical
`firewire`/`rushhour` evidence. Start by measuring admissible elimination
opportunities on the current code. Defer if too few exist; reject a design if
restoration costs erase the benefit or correctness/previously solved sessions
regress. Increasing the conflict budget alone does not test this mechanism.

Acceptance requires fresh [fuzzing gates](../development/correctness.md#choose-a-fuzz-gate)
and separate [native SAT-model validation](../development/validation.md). Preserve
the default behavior and compare against the unchanged solver.
