# Assignment migration evidence

This page records the September 2026 migration to one observed-assignment owner
per SAT connection. The current design is explained in
[Shared assignment ownership](../implementation/dpllt.md#shared-assignment-ownership).
The results below concern that migration; the later
[publication qualification](publication.md) is a separate sample.

## Measured revisions and coverage

| Role | Revision |
| --- | --- |
| Assignment ownership implementation | `45e25a6` |
| Measured candidate, with identical C++ sources | `e4abefb` |
| Comparison baseline | `fd4d1eb` |

The run-log entry `2026-09-25-shared-assignment-slurm` in the
[run ledger](../run-log.json) records the exact commands, resources, job IDs,
binary hashes and artifacts. Accepted solver batches ran on SLURM; the
interrupted local attempt remains preliminary evidence.

The checks covered six logics, retained sessions, generated quantified inputs
and real-seed mutations. All 43 normal and ASan test groups passed. The
[migration review](../review-history.md#shared-assignment-migration-review)
records the interface-atom observation repair and protocol regressions.
The fuzz records distinguish passes from resource limits and unusable reference
answers; they do not establish that every generated input was decided.
TypeFuzz's 1,615 malformed UF mutants are excluded from passing-case counts.

## Comparison and model-validation limits

The fixed 120-input comparison retained the same solved counts and had no
answer disagreements in four profiles. The default and offline-Int profiles
exceeded the comparison's strict 1% threshold for runtime on commonly solved
inputs, with increases of 1.05% and 1.23% respectively. Default mixed-search
counters could also change. RSS included the benchmark harness and did not
isolate the trail's memory cost. These results support neither a speedup claim
nor a full-corpus performance conclusion.

Independent model replay covered all 240 successful SAT rows: 220 direct
Dolmen passes and 20 passes after widening the validator logic to `QF_NRA`.
Those 20 rows came from five generated LRA inputs rejected under their declared
linear syntax because of constant negative denominators. The saved assertions
and models were unchanged. Strict declared-logic validation remains incomplete
for those rows; they are not counted as direct passes.

The {source}`comparison manifest <benchmarks/shared-assignment-20260925/manifest.json>`
and {source}`summary <benchmarks/shared-assignment-20260925/comparison-summary.json>`
retain the measured profiles and inputs.
The run ledger also identifies the separate reference-confirmation and
model-replay stages. These historical records describe the migration's checks;
they are not part of the assignment interface or its lifetime rules.
