# Experiments

Experiment and benchmark reports belong here.

Use MyST Markdown for written reports. Use notebooks only for executable benchmark analysis or plotted reports.
Record every nontrivial benchmark, fuzzing, profiling, tuning, dependency-profile,
or reference-comparison run in `docs/run-log.json`; see [](run-log.md) for the
schema and evidence policy.

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
runner sends `SIGTERM` at the timeout and waits briefly before `SIGKILL`, so
Primo can flush partial `--stats` counters for timeout rows. The
same parser preserves LLM2SMT `[stats]` lines as `llm2smt.*` columns for
reference comparisons.
script uses SMT-LIB `:status sat|unsat` metadata as the expected answer by
default; pass `--no-expected-from-status` to disable that and rely only on
`--expected`. The summary reports total solved, status and answer counts, wrong
answers, PAR2, solved counts by family, and totals for every emitted `--stats`
counter.

For cluster runs, submit `scripts/slurm_interesting_qf_uf.sbatch` from a built
remote checkout. It uses a 32-way SLURM array over the exact manifest. Monitor
progress with `squeue`, shard CSV counts in the result directory, and `sacct`
after completion; do not run a local sleep loop on the login node.

Before submitting a SLURM benchmark, prepare and validate the remote binary with
the repository preflight script:

```bash
python3 scripts/slurm_prepare_benchmark_workspace.py \
  --ssh-config /tmp/llm2smt_slurm_ssh_config \
  --remote-dir <slurm-workdir>
```

The script refuses a dirty local tree, syncs the committed source, rebuilds
CaDiCaL on the cluster, rebuilds `build-slurm-rel/primo`, and checks that
`primo --version` reports the local commit and a CaDiCaL version. Use
`--check-only` immediately before `sbatch` when the workspace was already
prepared. Destructive sync/rebuild is refused while active SLURM jobs with Primo
job names are present, because those jobs may be using the same `build-slurm-rel`
binary. Use a separate remote workdir for overlapping campaigns; reserve
`--allow-active-jobs` for cases where the active jobs are known to use another
workspace.

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

Pass `--manifest path/to/files.txt` for a reproducible subset. Manifest rows may
be absolute paths or paths relative to `--root`; a missing `.smt2` suffix is
added automatically. Blank lines and text after `#` are ignored.

On SLURM, run one shard per array task and merge shard CSVs after all tasks
complete. The CSV records file, family, solver status, answer, SMT-LIB
`:status` expectation, wrong-answer flag, elapsed seconds, timeout flag, exit
code, a short stderr prefix, and any `--stats` counters emitted before
completion or during the timeout `SIGTERM` grace period. The summary records solved, unsupported,
wrong-answer, status, answer, family, and PAR2 counts. Only non-wrong `sat` and
`unsat` answers count as solved or as elapsed-time PAR2 successes; `unknown`,
`unsupported`, timeouts, and errors contribute `2 * timeout`. Any performance
report based on this archive must include the SLURM job id, timeout, commit,
merged CSV path, family summary, unsupported count, wrong-answer count, and
comparison against the previous full-archive Primo run.

Generated SLURM scripts are part of the benchmark scaffold. Write them to a
local file, validate them, then copy and submit:

```bash
python3 scripts/slurm_validate_sbatch.py /tmp/primo-qfuf.sbatch
python3 scripts/slurm_prepare_benchmark_workspace.py \
  --ssh-config /tmp/llm2smt_slurm_ssh_config \
  --remote-dir <slurm-workdir> \
  --check-only
scp -F /tmp/llm2smt_slurm_ssh_config /tmp/primo-qfuf.sbatch \
  llm2smt-slurm:<slurm-workdir>/
ssh -F /tmp/llm2smt_slurm_ssh_config llm2smt-slurm \
  'cd <slurm-workdir> && sbatch primo-qfuf.sbatch'
```

Use quoted delimiters for embedded interpreter heredocs, such as
`python3 - <<'PY'`. Do not rely on nested shell quoting for embedded Python in
SLURM campaigns; that has produced immediate array failures before.

After each native full-archive run, rank fast-reference/slow-Primo gaps before
selecting a target instance:

```bash
python3 scripts/rank_qfuf_gaps.py \
  --native results/qf-uf-full/merged.csv \
  --ref results/slurm-qfuf-full-ref-z3-109856/z3-qfuf-full-2s-merged.csv \
  --ref results/slurm-qfuf-full-ref-cvc5-109857/cvc5-qfuf-full-2s-merged.csv \
  --timeout 2 \
  --fast-ref 1 \
  --top 50 \
  --out results/qf-uf-full/fastref-slowprimo.csv
```

Use the ranked CSV as the triage queue. Prefer clusters of high-ratio rows from
the same family over isolated one-offs, and carry the listed solver stats into
the local diagnosis.

## CaDiCaL Profile Check

`scripts/check_cadical_profile.py` is a campaign guard for dependency/version
sensitivity in the SAT backend. It compares Primo's default CaDiCaL settings
against an incremental profile:

```bash
python3 scripts/check_cadical_profile.py \
  --solver ./build/primo \
  --timeout 10 \
  --out results/cadical-profile/current.csv \
  --summary results/cadical-profile/current.md \
  tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk033.smt2 \
  tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2 \
  tmp/QF_UF/NEQ/NEQ041_size6.smt2
```

The incremental profile sets `ilb=2` and disables `sweep`, `congruence`,
`backbone`, `condition`, `elim`, `vivify`, `probe`, `subsume`, `transred`, and
`walk`. This mirrors the CaDiCaL-version debugging lesson documented in
<https://mikolasjanota.github.io/just_count/investigation.html>: `ilb` changed
defaults between CaDiCaL versions, and short-timeout throughput checks exposed
the regression quickly. For Primo this script is not a headline benchmark and
does not change CLI defaults. Use it to decide whether full QF_UF SLURM
campaigns should include a CaDiCaL-profiled variant.

To compare CaDiCaL releases rather than only option profiles, build one Primo
binary per CaDiCaL tag:

```bash
python3 scripts/check_cadical_versions.py \
  --tags rel-1.9.5 rel-2.2.1 rel-3.0.0 \
  --timeout 10 \
  --out results/cadical-version-profile/profile.csv \
  --summary results/cadical-version-profile/profile.md \
  tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk033.smt2 \
  tmp/QF_UF/2018-Goel-hwbench/QF_UF_firewire_tree.3.prop2_ab_reg_max.smt2
```

This is a dependency check, not a solver algorithm. Record the resulting job,
commands, tags, and artifact paths in `docs/run-log.json` before citing the
comparison.

## QF_UF Parameter Tuning With irace

`scripts/irace/` contains a reproducible irace setup for QF_UF profile tuning.
It is meant for SLURM runs, not workstation benchmarking. The training manifest
is generated from existing benchmark artifacts:

```bash
python3 scripts/irace/make_qfuf_tuning_manifests.py
```

This writes `benchmarks/qfuf-irace-train.txt` and
`benchmarks/qfuf-irace-test.txt`. The training set combines current
NEQ/PEQ/SEQ timeouts, solved-but-slow cases against LLM2SMT, and easy guard
instances. The generator interleaves guard, slow-gap, and timeout instances so
early irace races see discriminating costs instead of only uniform timeout
penalties. `qfuf_initial.txt` seeds the race with the current CLI default as
the first configuration, plus conservative nearby variants; any tuned elite
must beat that seeded default on the holdout before it can be considered. The
target runner uses SMT-LIB `:status` metadata when available: correct solved
runs cost elapsed seconds, timeouts/errors cost PAR2, and wrong answers receive
a large penalty.

On the cluster, install irace in a user-local environment, then run from the
repo root:

```bash
export MAMBA_ROOT_PREFIX=$PWD/.micromamba
export PRIMO_TUNE_SOLVER=$PWD/build-slurm-vp/primo
export PRIMO_TUNE_TIMEOUT=30
export PRIMO_TUNE_WRONG_PENALTY=30000
tools/micromamba/bin/micromamba run -n irace \
  Rscript -e 'args <- commandArgs(TRUE); args <- args[args != "--args"]; do.call(irace::irace_cmdline, list(args))' --args \
  --scenario scripts/irace/qfuf_scenario.txt \
  --parallel 4 \
  --max-experiments 600
```

Do not promote an irace elite directly to a default. First validate it on the
holdout manifest, the full NEQ/PEQ/SEQ set, and then the full QF_UF archive.
Report wrong answers, PAR2, solved counts by family, and the exact generated
Primo command line.

## QF_UF Gap Tuning With SMAC

`scripts/smac/qfuf_gap_tune.py` runs focused SMAC campaigns for current QF_UF
gaps against reference-solver evidence. It uses SMAC's instance support: each
training manifest row is a separate SMAC instance, the default configuration is
included in the race, and the target function runs one solver invocation per
configuration-instance trial. The validation manifest contains held-out rows
from the same benchmark shape; optional guard manifests record benchmark shapes
that must not regress.

Run it on SLURM, not on a workstation:

```bash
python3 -m venv .venv-smac
. .venv-smac/bin/activate
python -m pip install 'smac>=2,<3'

python scripts/smac/qfuf_gap_tune.py \
  --repo <slurm-workdir> \
  --solver <slurm-workdir>/build-slurm-rel/primo \
  --train benchmarks/qfuf-smac-gap-train.txt \
  --validate benchmarks/qfuf-smac-gap-validate.txt \
  --out-dir results/smac/qfuf-gap-c2c7e0e \
  --timeout 45 \
  --validation-timeout 120 \
  --trials 80
```

The same invocation can be submitted through `scripts/slurm_qfuf_smac.sbatch`
by setting `PRIMO_SMAC_TRAIN`, `PRIMO_SMAC_VALIDATE`, `PRIMO_SMAC_LABEL`,
`PRIMO_SMAC_TRIALS`, `PRIMO_SMAC_TIMEOUT`, and
`PRIMO_SMAC_VALIDATION_TIMEOUT`. Set `PRIMO_SMAC_GUARD` and
`PRIMO_SMAC_GUARD_TIMEOUT` when tuning a narrow family while preserving another
family. For QG `iso_*` tuning, use
`PRIMO_SMAC_GUARD=benchmarks/qfuf-smac-equality-guard.txt` so the diamond
equality/finite-domain families reject profiles that lose equality-bridge
behavior.

The objective is per-instance PAR2, with wrong answers penalized far above
timeouts. After the SMAC budget is exhausted, the script validates the SMAC
incumbent and the current default on the held-out instances and writes
`result.json`, `incumbent.json`, `trial-summary.csv`, `target-runs.csv`, and
`validation-runs.csv`. If a guard manifest is provided, `result.json` sets
`rejected_by_guard` when the incumbent has more wrong answers, fewer solved
rows, or higher guard PAR2 than the default.

For profile search, prefer the mixed manifests
`benchmarks/qfuf-smac-mixed-train.txt` and
`benchmarks/qfuf-smac-mixed-validate.txt`. They combine QG `iso_*` gaps,
NEQ/PEQ/SEQ finite-domain equality gaps, and Goel hardware sentinels, so a
candidate cannot optimize only one family while ignoring known regressions.

Use `scripts/slurm_qfuf_phase_compare.sbatch` for a narrower default-versus
`--theory-phase=auto` validation on the mixed train/validation/equality-guard
union. It runs the current default and `--theory-phase=auto` over the same
de-duplicated manifest, writes one result directory per variant, and is intended
as the fast guard before considering whether the auto phase heuristic is ready
for a broader 334-row or full-QF_UF campaign. Each SLURM array task writes its
own manifest through a temporary file and atomic rename; the manifest must not
be shared across tasks because concurrent truncation can make valid paths appear
missing.

Use `scripts/slurm_qfuf_portfolio_compare.sbatch` for the matching
default-versus-`--qf-uf-portfolio=basic-polarity` validation. It uses the same
manifest handling and output layout as the phase comparison script, with
`PRIMO_PORTFOLIO_*` environment variables replacing `PRIMO_PHASE_*`.

Use `scripts/slurm_qfuf_option_compare.sbatch` for ad hoc option comparisons
over the same sharded QF_UF harness. `PRIMO_OPTION_VARIANTS` is a
space-separated list of `name=arg1,arg2` records. The default compares
`default`, `--qf-uf-preprocess=basic`, and
`--qf-uf-preprocess=basic,--theory-phase=polarity`. This script is intended for
evidence collection before promoting a preprocessing or phase heuristic to the
default profile.

The narrower `benchmarks/qfuf-smac-iso-train.txt` and
`benchmarks/qfuf-smac-iso-validate.txt` manifests are diagnostic only. They
include the user-visible `QG-classification/qg7/iso_brn_repgen033.smt2` case,
ranked slow `iso_brn_nogen_sk*` siblings, and `iso_icl_repgen006.smt2`. Do not
promote an iso-only incumbent; rerun with the mixed manifests, the equality
guard, and then the broader 334-row/full-QF_UF validation. The search space
includes preprocessing strength, finite-domain symmetry/value-precedence and
equality-definition toggles, bounded ground-congruence, EUF engine/propagation
settings, theory phase, and the existing CaDiCaL incremental profile.

The first completed campaign for commit `c2c7e0e` found an incumbent that solves
`NEQ015_size6` in 25.36s under the 120s validation cap, while the default timed
out on all seven validation instances. The incumbent did not solve the remaining
six validation cases. Treat this as a diagnostic signal, not a default change:
the winning profile uses aggressive online EUF propagation and the incremental
CaDiCaL profile, so the engineering target is a cheaper, principled propagation
queue rather than exposing another broad default profile.
