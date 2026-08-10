# Run Log

`docs/run-log.json` is the canonical lightweight ledger for Primo experimental
evidence. Use it for benchmark campaigns, fuzzing, profiling, parameter tuning,
SAT dependency profile checks, and reference-solver comparisons.

The ledger is intentionally small JSON, not a database service. It should be
updated in the same commit as the script, manifest, documentation, or solver
change that relies on the run. Large raw artifacts may stay under `results/`,
`fuzz-results/`, or on the SLURM workspace, but the tracked JSON entry must make
the run discoverable and reproducible.

## Required Fields

Each entry should record:

- stable `id`, preferably `YYYY-MM-DD-short-description`;
- `started_at` and `recorded_at`;
- `kind`: `benchmark`, `fuzz`, `profile`, `tuning`, `dependency-check`, or
  `reference-comparison`;
- benchmark or fuzzing `scope`;
- Primo `commit`;
- environment type, host, and SLURM job id when applicable;
- exact command line, or `unknown` only for historical backfill;
- solver and reference solver command lines;
- input source: manifest, generator, seed, count, logic, and timeout;
- artifact paths for summaries, merged results, logs, and failure directories;
- summary counts such as solved, wrong answers, timeouts, PAR2, pass/crash
  counts, and family summaries when available;
- `evidence_status`: `current`, `historical`, `needs_revalidation`,
  `rejected`, or `blocked`;
- notes explaining limitations, missing metadata, or rejected conclusions.

## Policy

Do not cite a run in a final answer, commit message, design note, or
retrospective unless it has a run-log entry. If a run was discovered from old
artifacts and lacks metadata, add it with `evidence_status:
needs_revalidation` instead of treating it as current evidence.

For solver behavior changes, fuzz entries must be fresh for the current commit.
Old `bugs/` directories must be revalidated and classified before the run can
move from `needs_revalidation` to `current`.

For user-requested SLURM campaigns, local runs may be logged as profiling,
debugging, or smoke checks, but headline benchmark evidence must reference the
SLURM job id and artifact directory.

## Sharded benchmark summaries

`scripts/summarize_benchmark_shards.py` aggregates the durable CSV files
written by the benchmark harness. It ignores `*-summary.csv`, rejects duplicate
benchmark paths, reports totals and family-level solved/timeout/error/PAR2
counts, and can compare recoveries, losses, decisive-answer disagreements, and
common-solved time for two runs. This keeps the analysis reproducible without
rerunning unchanged reference solvers:

```bash
python3 scripts/summarize_benchmark_shards.py \
  --timeout 30 \
  --run baseline=results/baseline \
  --run candidate=results/candidate \
  --baseline baseline \
  --candidate candidate
```

The JSON printed by the script is an analysis aid, not a replacement for a
`docs/run-log.json` entry. Raw result directories may remain remote or
untracked, but the ledger still records the exact commit, environment, command,
artifact path, and accepted or rejected conclusion.
