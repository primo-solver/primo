# Debugging and investigating regressions

First distinguish the failure: wrong answer, invalid model, crash, rejected
valid input, timeout, or infrastructure failure. Keep the input and exact
command before changing options. Read stdout and exit status together; an
exception after printing an answer is still a failed run.

## Find the failing layer

- Syntax/name/sort defects: inspect [frontend](../implementation/frontend.md)
  and command-state behavior before assuming a theory error.
- Invalid SAT models: retain the printed response and use
  [independent validation](validation.md); inspect reconstruction as well as
  the final theory assignment.
- UNSAT disagreements: minimize the input, check the reference, and examine
  generated clauses and their explanations. A disagreement does not establish
  which solver is wrong.
- Lifetime crashes: use a debug/sanitizer build and the ownership map. Snapshot
  IDs before term creation and reacquire borrowed storage after allocation.
- Performance regressions: check effective profiles and mechanism counters,
  then profile the dominant phase. Do not optimize on an unresolved correctness
  failure.

The [statistics reference](../reference/statistics.md) explains counters;
the [fuzzing guide](fuzzing.md#triage-and-reduction) explains reduction.
Memory-sensitive changes require the repository's ASan and allocation-boundary
regressions, not only a successful release run.

## Ordering sensitivity

Semantically equivalent inputs can receive different term IDs and encounter
orders. Those orders can affect polynomial assembly, arithmetic-variable IDs,
pivot tie-breaking, clause insertion, and SAT search. Splitting one conjunction
into multiple assertions can also change legacy preprocessing selection.

Compare answers first, effective profiles second, search counters third, and
runtime last. Do not infer a data-race or a specific heuristic from a timing
difference alone. Use structural analysis and controlled perturbations to
identify the changed ordering. The
{source}`ordering investigation <docs/termid-order-sensitivity.md>` retains
concrete causes, fixes, and inconclusive experiments. Its old hotspots require
reprofiling before new optimization work.

## Bisect through the real harness

Verify a known good and known bad revision with identical inputs, dependency
versions and limits. Build each revision where the compiler and headers are
available; run the test in the intended execution environment. Keep build
failure distinct from reproduction failure and use the bisect skip status for
an untestable revision. Record the exact predicate and both endpoint results.

Historical cluster failures included missing shell initialization, tools only
available on the submission host, missing C++ headers on workers, unavailable
timing utilities, corpus paths resolving outside the workspace, and missing
result shards. Check these assumptions explicitly rather than carrying old
host-specific paths into the procedure. The dated incidents remain in the
{source}`original bisect record <docs/debugging.md#L1397>`.

## Finish a repair

Add a regression for the actual invariant, explain the fix in the owning
implementation chapter, and run the applicable checks and fresh fuzz gate.
Preserve the original failure and reference outcome with the run record.
Resource-inconclusive or dependency-blocked checks stay incomplete. The
[contribution guide](contributing-changes.md) describes the review boundary.
