# Full-corpus evaluation at longer limits

The October 22 campaign evaluates every supported non-incremental division at
**1,200 seconds per input**. The initial timing attempt stopped after an OpenSMT
QF_LIA process segfault. October 23 recovery retained 158 certified timing shards
and resubmitted the remaining 6,304. A live-journal parsing error then stopped
that recovery attempt. Infrastructure failures are now isolated; the next
recovery retained 182 certified complete shards and resubmitted the remaining
6,280. Model replay and the final comparison remain pending. The generated gate passed 5,972 cases; 28 QF_LIA
cases timed out at its separate five-second limit. It found no wrong answers,
crashes or invalid models. Run `2026-10-22-all-theories-long-limits`
in the [run ledger](../run-log.json) records exact snapshots, jobs and artifacts.

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
allocations. Each timed process receives one physical CPU and 8 GiB on the
compute partition; GPU nodes are excluded. Primo and references receive the
same 1,200-second wall limit. A shard contains at most 24 inputs and receives
nine hours; that allocation limit is distinct from the per-input limit.

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

The journal guard stops the campaign on Primo hard failures, wrong answers, or
opposite definite answers from any two profiles. A reference process crash or
parser/execution error is retained as an **error row**, with its exit status and
raw stdout/stderr. It receives the unsolved PAR2 penalty and never contributes
a solve or a corroborating answer. These external execution failures do not
cancel unrelated timing tasks. CPU job `225508` also passed the crash-then-success test with infrastructure
isolation enabled. Global cancellation is restricted to an explicit
correctness-failure type; harness or filesystem failures stop only their own
task and are recorded separately. Unreadable live-journal snapshots are retried
as advisory observations. Every terminal CSV is still checked strictly before
a task succeeds, and the global audit requires complete coverage. The final
audit includes each reference error row and hashes
its raw logs; missing evidence still prevents result acceptance.

The first workflow used an overly broad guard: an OpenSMT SIGSEGV on
`20210219-Dartagnan/ReachSafety-Loops/benchmark51_polynomial-O0.smt2` canceled all
arrays after 158 timing shards completed. That attempt and its partial rows are
preserved. CPU job `225341` reproduced that SIGSEGV under the repaired guard, then
successfully solved the following known-good input. The regression suite also
checks that native failures and answer contradictions remain blocking.

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
Each model task records that barrier's hash. Wrong answers, native failures,
reference disagreements and invalid models block result acceptance.

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
