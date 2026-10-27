# Full-corpus evaluation at longer limits

The evaluation covers every supported non-incremental division at **1,200
seconds per input**. The October 22–23 attempt stopped with 429 complete timing
shards after a Primo process was killed on quantified UF. Its memory reservation
had not established an effective hard cap. Those timings remain preserved as
an uncapped historical cohort and are excluded from the new bounded comparison.

The October 24 replacement uses a hard **8 GiB virtual-address-space limit per
process**. All six inventories and admission probes are complete, and the
replacement timing arrays are running. Initial worker provenance confirms both
limits are exactly 8 GiB. Full timings, model replay and the final comparison
are not complete. Runs `2026-10-22-all-theories-long-limits` and
`2026-10-24-all-theories-enforced-memory` in the [run ledger](../run-log.json)
record the separate attempts, exact snapshots, limits, jobs and artifacts.

| Division | Inputs |
| --- | ---: |
| QF_UF | 7,503 |
| QF_LRA | 1,753 |
| QF_LIA | 13,306 |
| QF_UFLRA | 1,284 |
| QF_UFLIA | 659 |
| UF, quantified | 7,590 |
| Total | 32,095 |

This is the full existing corpus, with no competition selection or easy-input
filter. Existing copies are reused read-only and freshly hashed in CPU SLURM
allocations. Each timed solver process uses one physical CPU and inherits an
8 GiB soft and hard `RLIMIT_AS`; the batch scripts set `ulimit -v 8388608` before
starting Python. Worker provenance records both byte limits, and workers and
audits reject a missing or different limit. The SLURM reservation remains
8 GiB per task; GPU nodes are excluded. This is a per-process virtual-address-
space limit, including mappings and libraries.

CPU probe `242893` on the failed job's node found `memory.max=max` throughout
the job cgroup hierarchy, even though SLURM requested 8 GiB and its configuration
said to constrain RAM. Probe `242895` then verified the explicit process limit:
both `RLIMIT_AS` values were 8,589,934,592 bytes, and a 9 GiB anonymous mapping
was rejected with `ENOMEM`. No large physical allocation was needed for that
check. The old 429 complete shards cannot establish compliance with this new
limit and are not reused as bounded timings. Input corpora and preserved solver
executables can be reused with matching hashes.

Primo and references receive the same 1,200-second wall limit and hard address-
space limit. A shard contains at most 24 inputs and receives nine hours; that
allocation limit is distinct from the per-input limit.

## Profiles and references

The native implementation is frozen at `4419dc9`, with the exact previously
qualified binary preserved. Current defaults are measured in every division.
QF_LRA also measures `--lra-model-phase --monotone-elimination`, its strongest
recent full-corpus profile. The equality experiment remains outside this
comparison, and no solver defaults change.

The reference candidates are Z3, cvc5, Yices and OpenSMT, each with its defaults.
Versioned binaries are preserved before measurements. Admission probes run
separately, retain diagnostics and exercise SAT and UNSAT cases. A reference
that rejects a division is reported unavailable there, rather than assigned
zero solves. Quantifier probes must require real instantiation: trivial
quantified formulas can simplify away without testing general admission.
Admission does not establish completeness or support for every input feature.

Each cactus will select **one static Primo profile for the entire division**:
highest solved count, then lowest PAR2, then profile name to break a tie.
Both native profiles remain in the audit where measured. There is no per-input
oracle selection or implied shared-budget portfolio. Integer and quantified
instance limits retain their existing values; `unknown` remains unsolved even
when it is returned before the wall limit.

## Correctness and phase barriers

A fresh 6,000-case generated gate covered the six routes before headline timings. Existing
release/ASan checks and the QF_LRA option qualification refer to the identical
native binary. Capability probes retain Primo's documented incomplete quantified
SAT behavior separately from wrong answers and unsupported logics.

The maintained {source}`timing/model adapter <scripts/benchmark_model_replay.py>`
records each input and executable hash, exact arguments and parsed answers in
CSV/journals. Raw timing stdout/stderr is retained for timeouts and errors;
successful timing rows do not retain complete raw streams. Model replay retains
its separate printed-model and validator artifacts. Campaign
execution records add hardware, resources, immutable plan identity and global
shard identity. Explicit small manifests prevent every worker from traversing
the whole corpus; the shared adapter processes each supplied manifest as one
local shard. The campaign audit checks the global disjoint coverage.

The journal guard cancels the campaign on a wrong answer or opposite definite
answers from two profiles. Process crashes and execution errors, including
native failures, remain **unsolved error rows** with their exit status and raw
stdout/stderr; they do not cancel unrelated tasks. Recognized explicit allocation
failures, such as `std::bad_alloc`, are counted separately as memory-out results.
All these outcomes receive the unsolved PAR2 penalty and never corroborate an
answer. The final audit hashes the retained raw logs.

Native execution errors block headline claims and default promotion. The final
report names the affected divisions/profiles and labels their plots diagnostic.
Successful SAT rows still require their separate model checks; a diagnostic
comparison does not qualify the failing profile. A deliberately reduced 64 MiB
control on the earlier failing UF input produced a CaDiCaL cleanup abort rather
than a clean allocation-failure message. It is retained as a native error under
forced small memory, not counted as a memory-out pass. The actual evaluation
limit is 8 GiB. This task changes the harness, not the native implementation.

Two October 26 diagnostic replays then established the allocation-failure path
at the **actual 8 GiB evaluation limit**, using the preserved binary and original
input hashes. The {download}`exception replay script <../../scripts/qualification/long_limits/trace_oom.sbatch>`
uses GDB on CPU SLURM allocations; its target inherits exactly 8 GiB soft and
hard address-space limits, while the debugger has a separate 16 GiB budget.
The debugger stops at the first C++ throw and records the allocation stack and
target process limits before continuing to the abort.

| Replayed division/input | Failing allocation |
| --- | --- |
| QF_LIA: `nec-smt/large/getoption_user/prp-23-48.smt2` | Binary-clause vector growth in SAT preprocessing |
| UF: `sledgehammer/FFT/uf.706467.smt2` | Term-vector growth during quantifier instantiation |

Both traces stop in the allocation-failure path of `operator new`, then abort
while {equalityexperiment}`AssignmentConnection <src/smt/assignment_coordinator.hpp>`
is destroyed and calls external-propagator disconnection during exception
unwinding. These exact runs are confirmed memory exhaustion followed by a
cleanup failure; the QF_LIA trace does not establish integer branching as the
cause. GDB could not print the exception's C++ type without library debug info,
but the allocation origin and subsequent abort stacks were retained.

The contemporaneous native CSV census found 266 explicit `std::bad_alloc` rows
and 652 rows with the same cleanup diagnostic. The two replays establish the
mechanism for their exact input/profile/binary combinations; the matching
message alone does not individually confirm every remaining abort. Confirmed
memory-outs are ordinary unsolved budget exhaustion. Raw timing rows retain
their original exit codes, and the generic report still counts cleanup aborts
as execution errors; the case-specific triage is recorded separately in the run
ledger. These diagnostic runs contribute no speed measurements, and full
SAT-model qualification remains pending.

The two exact inputs are also recorded as
[memory-attribution candidates](../leads/incremental-memory.md#current-memory-exhaustion-candidates).
The UF case motivates term-storage and reclamation profiling; the QF_LIA case
helps distinguish backend storage pressure. Neither trace establishes that
reference counting would avoid the failure.

Harness or filesystem failures stop only their own task and are recorded
separately. Unreadable live-journal snapshots are advisory and retried; every
terminal CSV and the global coverage audit remain strict. Earlier CPU smoke
jobs `225341` and `225508` established that a reference SIGSEGV can be followed
by a successful solve. The memory probe and metadata regressions cover the new
limit and failure classification.

The first workflow used an overly broad guard: an OpenSMT SIGSEGV on
`20210219-Dartagnan/ReachSafety-Loops/benchmark51_polynomial-O0.smt2` canceled all
arrays after 158 timing shards completed. That attempt and its partial rows are
preserved. CPU job `225341` reproduced that SIGSEGV under the repaired guard, then
successfully solved the following known-good input. The regression suite also
checks that execution failures remain unsolved while answer contradictions
remain blocking. Native failures block headline/default qualification.

The {longlimitsworkflow}`recovery tool <scripts/qualification/long_limits/recover.py>`
leaves the original plan, limits, executable snapshots and corpus
inventories unchanged. It validates complete timing shards with the same
coverage and provenance audit used by reporting, then freezes hashes of each
reused CSV, provenance and execution record. Interrupted shard directories and
the original cancellation records move to an explicitly recorded archive;
only unfinished shards are rescheduled. An immutable recovery record binds the
old plan and worker identity to the repaired worker and its pending task list.
Reports verify reused hashes and the recovery identity for new executions.
Further recovery attempts archive the prior recovery record, retain its worker
and record hashes as explicit ancestors, and recertify all completed shards;
identical repeat submission of the same worker is rejected.

The global timing audit runs after all timing arrays terminate, including
failed arrays, and must accept all shards before any model replay starts.
Each model task records that barrier's hash. Wrong answers, decisive
reference disagreements and invalid models block result acceptance. Native
execution errors block headline/default qualification even when diagnostic
plots and successful-row model coverage are available.

Every successful Primo SAT row in the five ground divisions receives a
separate direct Dolmen check in the original logic, using the same native
arguments plus model production and internal checking. Model production gets
1,800 seconds and Dolmen 300 seconds, with Dolmen's size budget set to 5G
inside the 8 GiB allocation. Missing or incomplete checks prevent full ground
qualification; checking time never enters measured runtime or PAR2.

Primo does not print models for quantified formulas. Quantified UF therefore
receives reference-answer corroboration and explicit uncorroborated-answer
counts, **without independent printed-model qualification**. Ground model
passes must not be generalized to this route.

## Progress cactus snapshots

The {download}`snapshot exporter <../../scripts/qualification/long_limits/preview.py>`
can plot all six divisions before the campaign finishes, without advancing the
strict timing/model barriers. It writes a fresh output directory and reads the
measurement tree without modifying its rows or original phase reports. Run it
in the CPU SLURM audit allocation, with the same frozen worker adapter and a
fresh `PRIMO_PREVIEW_OUT`; the maintained audit batch wrapper invokes its
`preview` stage.

Within each division it selects only complete shard indices shared by every
planned solver/profile, including both native QF_LRA profiles. Every plotted
series therefore covers identical inputs; unknown, timeout, allocation-failure
and process-error rows remain in that cohort as unsolved. Each included shard
receives the normal strict timing-provenance audit, and definite answers are
cross-checked before plotting. One static Primo profile is selected by solved
count, then PAR2, then name on this cohort. There is no per-input oracle and no
easy-input filter.

The gallery and SVG captions distinguish full timing coverage from a partial,
completion-selected cohort. Full-corpus rankings can change when pending shards
arrive. Neither kind of progress plot establishes independent SAT-model
qualification. The exporter retains minimal series CSVs, exact input hashes,
source-file certificates, outcome/family summaries and its `selection.json`.
Pass that selection to `--selection` with a fresh output directory to reproduce
the same cohort. The run ledger records each saved snapshot and its limits.

## Recorded outputs

The {longlimitsworkflow}`campaign driver <scripts/qualification/long_limits/campaign.py>`
and {longlimitsworkflow}`configuration <scripts/qualification/long_limits/config.json>`
write fresh inventories, capability records, a frozen plan,
durable timing journals, per-shard provenance, separate model coverage, and
strict timing/final audits. The final stage generates one full-corpus cactus
per division, selects the native profile by the rule above, and reports solved,
SAT/UNSAT, timeout, unknown, error, PAR2 and family totals. Historical 30-, 60-
and 300-second measurements remain unchanged and are not substituted for fresh
1,200-second reference runs.

The {longlimitsworkflow}`submission controller <scripts/qualification/long_limits/launch.py>`
splits arrays below the scheduler's 1,001-task index limit and
keeps total timing concurrency at or below 128 physical CPUs. Model replay uses
a separate phase with at most 64 concurrent tasks. Failed dependencies cancel
later compute rather than leaving a nominally successful partial evaluation.
The {longlimitsworkflow}`final audit <scripts/qualification/long_limits/report.py>`
remains explicit about missing results and quantified-model
limitations. Submission alone is not completion.
