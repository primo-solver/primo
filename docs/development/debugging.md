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

## Sample a frozen binary without rebuilding

When hardware profiling tools are unavailable,
{profilesource}`profile_gdb_samples.py <scripts/profile_gdb_samples.py>` samples a
native executable through GDB's machine interface. Run it in the required
experiment environment; the maintained paired SLURM recipe is
{profilesource}`profile_pair.sbatch <scripts/qualification/integer_unification/profile_pair.sbatch>`.
It runs frozen baseline/candidate binaries in both orders, on one allocation,
and records executable, input and sampler hashes before checking them again.

```bash
python3 scripts/profile_gdb_samples.py --out results/profile-one \
  --timeout 180 --interval 0.2 --seed 1 -- \
  /path/to/frozen/primo --quiet --stats /path/to/input.smt2
```

The sampler owns the debugger and inferior processes. It periodically interrupts
the inferior, records up to 41 frames of the selected thread, and resumes it.
A seeded jitter avoids sampling at one fixed period. Debug-information network
downloads are disabled. Normal exit completes the profile; nonzero exit,
unexpected signals, or the deadline leave a failed profile. The cleanup path
kills any remaining inferior and reaps the debugger. Tests exercise normal
sampling, a program that prints an answer before failing, and deadline cleanup.

Each fresh output directory contains the raw `gdb-mi.log`, durable
`samples.jsonl` stack records and `summary.json` with the command, binary hash,
seed, sample count, exit event and instrumented wall duration. Inspect the raw
log for the solver's answer and counters; a completed profile alone does not
establish answer correctness. Preserve the input hash and build identity with
the experiment record.

These are **wall-clock stack observations**, not CPU events or call counts.
Debugger stops perturb execution; scheduling delays can bias the distribution,
optimized frames may be missing, and deep stacks can be truncated. Only the
selected thread is sampled. Do not use instrumented duration as benchmark time,
or infer a small regression's cause from a few differing samples. Use repeated
profiles to locate substantial costs, then test a focused hypothesis separately.

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
{source}`original bisect record <docs/debugging.md#L1405>`.

## Finish a repair

Add a regression for the actual invariant, explain the fix in the owning
implementation chapter, and run the applicable checks and fresh fuzz gate.
Preserve the original failure and reference outcome with the run record.
Resource-inconclusive or dependency-blocked checks stay incomplete. The
[contribution guide](contributing-changes.md) describes the review boundary.
