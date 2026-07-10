# Experiments

Experiment and benchmark reports belong here.

Use MyST Markdown for written reports. Use notebooks only for executable benchmark analysis or plotted reports.

## Interesting QF_UF Exact Set

`benchmarks/interesting-qf-uf.txt` is the source of truth for the 118-row
`interesting.html` QF_UF set. Use the manifest workflow for this target:

```bash
python3 scripts/bench_interesting_qf_uf.py \
  --solver ./build/primo \
  --solver-arg=--quiet \
  --solver-arg=--qf-uf-profile=tuned-interesting \
  --solver-arg=--euf-prop=model \
  --solver-arg=--stats \
  --timeout 30 \
  --manifest benchmarks/interesting-qf-uf.txt \
  --out results/interesting-qf-uf.csv \
  --summary results/interesting-qf-uf-summary.csv \
  --log-dir results/interesting-qf-uf-logs
```

The script defaults to that manifest unless `--discover` is passed. Each output
row records the manifest row number, absolute and repository-relative file path,
family, solver arguments, status, answer, elapsed seconds, exit code, per-file
stdout/stderr logs, and any `--stats` counters emitted before completion. The
script uses SMT-LIB `:status sat|unsat` metadata as the expected answer by
default; pass `--no-expected-from-status` to disable that and rely only on
`--expected`. The summary reports total solved, status and answer counts, wrong
answers, PAR2, solved counts by family, and totals for every emitted `--stats`
counter.

For cluster runs, submit `scripts/slurm_interesting_qf_uf.sbatch` from a built
remote checkout. It uses a 32-way SLURM array over the exact manifest. Monitor
progress with `squeue`, shard CSV counts in the result directory, and `sacct`
after completion; do not run a local sleep loop on the login node.

If the published `interesting.html` table is available, pass
`--reference-html path/to/interesting.html`. The script extracts table text with
standard-library HTML parsing and writes `results/interesting-qf-uf-reference.csv`
by default, preserving columns for solvers such as `llm2smt_sym2`, Z3, Yices,
and OpenSMT when those columns are present.

Headline performance reports must separate Goel, NEQ, PEQ, QG, and SEQ solved
counts. `--euf-prop=online` is excluded from headline numbers until it has zero
errors/crashes and zero wrong answers on the exact set and the QF_UF fuzz gate.

## Full QF_UF Archive

`scripts/bench_qf_uf_full.py` runs the full extracted `sandbox/QF_UF.tar.zst`
archive. The expected layout is `tmp/QF_UF/<family>/**/*.smt2`, produced by
extracting the archive's `non-incremental/QF_UF` subtree into `tmp/QF_UF`.

Use the full archive harness for headline QF_UF runs:

```bash
python3 scripts/bench_qf_uf_full.py \
  --root tmp/QF_UF \
  --solver ./build/primo \
  --solver-arg=--quiet \
  --timeout 2 \
  --shards 32 \
  --shard-index 0 \
  --out results/qf-uf-full/shard-0.csv \
  --summary results/qf-uf-full/shard-0-summary.csv
```

On SLURM, run one shard per array task and merge shard CSVs after all tasks
complete. The CSV records file, family, solver status, answer, SMT-LIB
`:status` expectation, wrong-answer flag, elapsed seconds, timeout flag, exit
code, and a short stderr prefix. The summary records solved, wrong-answer,
status, answer, family, and PAR2 counts. Any performance report based on this
archive must include the SLURM job id, timeout, commit, merged CSV path, family
summary, wrong-answer count, and comparison against the previous full-archive
Primo run.
