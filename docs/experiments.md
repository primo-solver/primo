# Experiments

Experiment and benchmark reports belong here.

Use MyST Markdown for written reports. Use notebooks only for executable benchmark analysis or plotted reports.
Record every nontrivial benchmark, fuzzing, profiling, tuning, dependency-profile,
or reference-comparison run in `docs/run-log.json`; see [](run-log.md) for the
schema and evidence policy.

(formula-analyzer)=
## Analyzing benchmark shape: use the analyzer, not grep

`scripts/analyze_smt2.py` reports the structure of an SMT-LIB file: what is
declared, the atom census classified by shape (unary bound / difference /
two-variable / general linear), per-atom Boolean polarity computed through
`not`/`=>`/`ite`/`xor`, per-variable one-sidedness, and guard protection (the
"this value is a don't-care under that flag" shape from
[](tropical-matrix-structure.md)).

```bash
python3 scripts/analyze_smt2.py FILE...        # human-readable
python3 scripts/analyze_smt2.py --json FILE    # machine-readable
python3 scripts/analyze_smt2.py --selftest     # verify the analyzer itself
```

**Prefer it over `grep` for any structural claim.** Two real errors motivated
writing it, and both are now regression cases in `--selftest`:

- `grep -o "(*" file` reported ~12,000 multiplications in a file containing
  none -- in a basic regular expression `*` is a quantifier, so the pattern
  matches "zero or more open parens" everywhere.
- A regex matching only *flat* atoms silently skipped every nested constraint
  such as `(<= (+ (* 2 x) y) 5)`, which is most of what `miplib` and
  `latendresse` contain. It reported 456 of 996 `danoint-166` reals as
  one-sided; the analyzer reports **0**, which is correct.

The design rule that follows: **the analyzer never silently ignores what it
cannot parse.** Unsupported constructs are counted as `opaque` and their
variables are forced to the conservative classification (two-sided, mixed
polarity), so an unhandled construct weakens a claim instead of falsifying
it. A nonzero `OPAQUE atoms` line in a report is a signal to look, not noise.

Validation: on `tropical-matrix/constraint-199552.smt2` it independently
reproduces both the previously hand-derived 774 guard-protected variables and
the separately recorded "70-72% of arithmetic atoms are unary or
two-variable" census figure (1,611 of 2,250, 71.6%).

## Non-standard unsolved benchmarks

A benchmark left unsolved because the solver ran out of time is an honest
result. One left unsolved because we could not **parse** it, or refused input
the logic permits, is a defect wearing a timeout's clothes: it costs a benchmark
exactly as much, and a solved-count table cannot tell the two apart.

SMT-LIB inputs are well-formed by construction, so any such failure is ours
until shown otherwise. Two found in August 2026, both by reading code rather
than by any benchmark report:

- `(/ x 2.0)` was rejected although it is linear -- `(* (1/2) x)` -- and both
  z3 and Yices accept it.
- `(set-option :verbosity (- 1))` was rejected although SMT-LIB 2.6 allows an
  s-expression attribute value.

`scripts/compare_solver_answers.py` now prints an
`=== unsolved for NON-STANDARD reasons ===` section: per solver, grouped by
cause with example files, plus a warning when the count is non-zero. A row
counts as non-standard when its `status` is neither `ok` nor `timeout` and its
answer is not an honest `unknown` (a solver giving up inside its budget is
legitimate).

**Check it before quoting solved counts or PAR2.** If a cause is genuinely out
of scope -- an unimplemented theory, say -- record that in writing rather than
letting it sit in an aggregate.


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
runner sends `SIGTERM` at the timeout and waits briefly before `SIGKILL`.
Primo's signal handler only records a cancellation request; the normal solver
control flow observes it through the SAT terminator and returns `unknown`.
Output and final statistics are therefore produced outside signal context. A
timeout may still be killed before a long non-SAT phase reaches a cancellation
boundary, so timeout rows must not be assumed to contain statistics. The same
parser preserves LLM2SMT `[stats]` lines as `llm2smt.*` columns for reference
comparisons.
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
  --remote-dir <remote-home>/primo-cadical-profile
```

The script refuses a dirty local tree, installs a `git archive` snapshot of the
committed source by default, provisions the required cluster-local dependencies
with `make contrib-required`, rebuilds `build-slurm-rel/primo`, and checks that
`primo --version` reports the local commit and a CaDiCaL version. If the cluster
does not provide CMake on `PATH`, the helper installs CMake into `.venv-build`
and prepends that environment for dependency and build commands. The archive
sync deliberately avoids copying the local `.git` directory, build trees, result
trees, fuzz output, and machine-local dependency installations. The remote build
receives the commit through `PRIMO_GIT_REVISION_OVERRIDE`, so stale generated
outputs in a reused workspace cannot make benchmark binaries report a spurious
`-dirty` suffix. Use `--sync-mode rsync` only for a deliberate legacy full-tree
sync.

When a workspace already has a verified cluster-local dependency installation,
pass `--reuse-dependencies`. The helper preserves `contrib/local`, installs the
clean tracked source snapshot, removes `build-slurm-rel`, and rebuilds Primo from
scratch. This avoids repeated CaDiCaL, GMP, ANTLR, and CLI11 provisioning without
risking stale project objects from preserved source timestamps. Before reusing the
installation, the helper checks the concrete CLI11, ANTLR4, IPASIR, CaDiCaL, and
GMP headers/libraries required by CMake. If any are absent, it automatically runs
`contrib-required` instead of starting a doomed build. Explicit
`--contrib-target` requests can be combined with reuse mode for independent,
idempotent tools such as `contrib-yinyang`; those targets run after the core
artifact check without rebuilding valid native dependencies. The IPASIR check
accepts either `ipasir_up.h` or the compatible `ipasir.h`, matching the
alternatives in CMake; requiring only the former would falsely reprovision a
valid CaDiCaL installation on every synchronization.

For example, a fuzz workspace that already has the native build dependencies
and Z3 but lacks the pinned YinYang environment can be prepared with:

```bash
python3 scripts/slurm_prepare_benchmark_workspace.py \
  --reuse-dependencies \
  --contrib-target contrib-yinyang \
  --require-tool .venv-yinyang/bin/typefuzz \
  --require-tool contrib/local/bin/z3
```

`contrib-required` is only the dependency set needed to build and run Primo. A
benchmark that names reference solvers under `contrib/local/bin` must either
reuse an existing matching reference result, or prepare the workspace with the
matching reference tool and require it in the preflight check. For Z3:

```bash
python3 scripts/slurm_prepare_benchmark_workspace.py \
  --ssh-config /tmp/llm2smt_slurm_ssh_config \
  --remote-dir <remote-home>/primo-cadical-profile \
  --with-z3
python3 scripts/slurm_prepare_benchmark_workspace.py \
  --ssh-config /tmp/llm2smt_slurm_ssh_config \
  --remote-dir <remote-home>/primo-cadical-profile \
  --with-z3 \
  --check-only
```

If the SMT-LIB corpus, timeout, and reference binary are unchanged, prefer the
standing practice of reusing the existing reference CSV instead of rerunning Z3
or cvc5 just because a Primo branch changed.

`scripts/bench_qf_uf_full.py` writes an append-only JSONL journal after every
completed instance, defaulting to `<out>.jsonl`, in addition to its final CSV
and summary. SLURM result collection should retain this journal: it preserves
all completed rows if a task is cancelled, times out at the scheduler level, or
loses its node before the final aggregate files are written. `--journal` can
select a different path when a wrapper has a fixed artifact layout.

The local CMake build refreshes a generated git-revision header on every `primo`
build; uncommitted source changes are reported with a `-dirty` suffix. Use
`--check-only` immediately before `sbatch` when the workspace was already
prepared. Destructive sync/rebuild is refused while active SLURM jobs with Primo
job names are present, because those jobs may be using the same `build-slurm-rel`
binary. Use a separate remote workdir for overlapping campaigns; reserve
`--allow-active-jobs` for cases where the active jobs are known to use another
workspace.

Archive-mode preparation intentionally excludes `tmp/`, so benchmark manifests
that refer to locally extracted SMT-LIB archives need a separate staging step.
Before transferring a large corpus, check for a complete copy in another remote
workspace and verify its file count, total bytes, and path/size manifest hash; a
server-side copy is preferable to another network transfer. Otherwise, use the
staging helper to copy only the listed files into the matching remote root,
without copying `.git` or unrelated archive content. The helper streams a gzip
archive, extracts into a staging directory before replacing the target, and
reports the path count without printing the full manifest:

```bash
python3 scripts/slurm_stage_manifest_files.py \
  --ssh-config /tmp/llm2smt_slurm_ssh_config \
  --remote llm2smt-slurm \
  --remote-dir <remote-home>/primo-cadical-profile \
  --root tmp/QF_UFLRA \
  --manifest benchmarks/qf_uflra-smtlib-sample-5-per-family.txt
```

To stage a complete extracted corpus without first creating a temporary
manifest, replace `--manifest ...` with `--all-smt2`. Paths are discovered below
`--root` and sorted before archiving.

When the corpus already exists in another workspace on the same remote host,
add `--remote-source-dir`. The helper computes the selected local path/size
fingerprint, sends only the path list over SSH, verifies the remote source has
the same count, total bytes, and fingerprint, and then performs the tar copy
between remote directories. It still builds a staging tree and replaces the
target only after every selected file has been copied:

```bash
python3 scripts/slurm_stage_manifest_files.py \
  --ssh-config /tmp/llm2smt_slurm_ssh_config \
  --remote llm2smt-slurm \
  --remote-source-dir <existing-remote-workspace> \
  --remote-dir <new-remote-workspace> \
  --root tmp/QF_UFLRA \
  --all-smt2
```

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
comparison against the previous full-archive Primo run. Because timeout status
takes precedence over child exit status, reports must also inspect
`timed_out_with_stats`, `exit_code`, and `stderr_head` to distinguish a graceful
cancellation from the subsequent forced kill.

Harness status is stricter than recognized stdout. An explicit `unsupported`
answer has status `unsupported`, including Primo's normal exit code 2 for an
unsupported fragment. A nonzero exit with any other answer, or a zero exit
without a recognized answer, has status `error`. Only a recognized answer from
a normal exit has status `ok`. The same classification is used by
`scripts/bench_interesting_qf_uf.py` and is covered by the Python tests run by
`make check`.

Use `scripts/render_benchmark_matrix_html.py` to turn one or more merged
benchmark CSVs into the compact detailed-results table used for solver/config
comparisons:

```bash
python3 scripts/render_benchmark_matrix_html.py \
  --title "QF_UF default vs disequality Ackermann" \
  --timeout 30 \
  --metadata commit=546738e \
  --metadata slurm_job=117301 \
  --variant default=results/qfuf-drop-diseqack-117301/default-merged.csv \
  --variant diseq_ack=results/qfuf-drop-diseqack-117301/diseq_ack-merged.csv \
  --strip-prefix tmp/QF_UF/ \
  --strip-suffix .smt2 \
  --out results/qfuf-drop-diseqack-117301/matrix.html \
  --summary-json results/qfuf-drop-diseqack-117301/matrix-summary.json
```

The renderer accepts any benchmark CSV with `file`, `status`, `answer`,
`seconds`, and `exit_code` columns. It computes solved, timeout/error, wrong,
win, unique, suboptimal, not-run, elapsed-time, and PAR2 summaries when
`--timeout` is provided. For Primo `--stats` CSVs, it also prints selected SAT
and EUF counters in each detailed cell and totals them below the table. Add
extra counters with repeated `--stat :counter.name` arguments when an experiment
needs a narrower mechanism-specific view.

Generated SLURM scripts are part of the benchmark scaffold. Write them to a
local file, validate them, then copy and submit:

```bash
python3 scripts/slurm_validate_sbatch.py /tmp/primo-qfuf.sbatch
python3 scripts/slurm_prepare_benchmark_workspace.py \
  --ssh-config /tmp/llm2smt_slurm_ssh_config \
  --remote-dir <remote-home>/primo-cadical-profile \
  --check-only
scp -F /tmp/llm2smt_slurm_ssh_config /tmp/primo-qfuf.sbatch \
  llm2smt-slurm:<remote-home>/primo-cadical-profile/
ssh -F /tmp/llm2smt_slurm_ssh_config llm2smt-slurm \
  'cd <remote-home>/primo-cadical-profile && sbatch primo-qfuf.sbatch'
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
  --repo <remote-home>/llm2smt-fuzz \
  --solver <remote-home>/llm2smt-fuzz/build-slurm-rel/primo \
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

The narrower `benchmarks/qfuf-smac-neq-train.txt` and
`benchmarks/qfuf-smac-neq-validate.txt` manifests are diagnostic for the
`NEQ016_size8` and nearby NEQ reference gaps. They deliberately keep
`NEQ016_size7` in validation because local evidence shows
`--pure-theory-filter=off` helps `NEQ016_size8` but regresses the size-7
predecessor. Use `benchmarks/qfuf-smac-neq-guard.txt` as the guard manifest; it
contains PEQ/SEQ equality rows, QG `iso_*` rows, and Goel sentinels. Do not
promote an NEQ-only incumbent unless it also passes the guard and then validates
on the mixed manifests and broader QF_UF subsets.

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
`default`, `--preprocessing-mode=basic`, and
`--preprocessing-mode=basic,--theory-phase=polarity`. This script is intended for
evidence collection before promoting a preprocessing or phase heuristic to the
default profile.

The narrower `benchmarks/qfuf-smac-iso-train.txt` and
`benchmarks/qfuf-smac-iso-validate.txt` manifests are diagnostic only. They
include the user-visible `QG-classification/qg7/iso_brn_repgen033.smt2` case,
ranked slow `iso_brn_nogen_sk*` siblings, and `iso_icl_repgen006.smt2`. Do not
promote an iso-only incumbent; rerun with the mixed manifests, the equality
guard, and then the broader 334-row/full-QF_UF validation. The search space
includes preprocessing strength, finite-domain symmetry/value-precedence and
equality-definition toggles, post-simplification finite-domain AMO, bounded
ground-congruence, EUF engine/propagation settings, EUF explanation search,
theory phase, and the existing CaDiCaL incremental profile.

The first completed campaign for commit `c2c7e0e` found an incumbent that solves
`NEQ015_size6` in 25.36s under the 120s validation cap, while the default timed
out on all seven validation instances. The incumbent did not solve the remaining
six validation cases. Treat this as a diagnostic signal, not a default change:
the winning profile uses aggressive online EUF propagation and the incremental
CaDiCaL profile, so the engineering target is a cheaper, principled propagation
queue rather than exposing another broad default profile.

## Baseline worktrees and the sync (learned 2026-08-01)

`slurm_prepare_benchmark_workspace.py` rsyncs with `--delete`, which
includes the remote tree's `.git`. **That destroys any git worktree
registered under it**: `.git/worktrees/<name>/` is removed, the worktree's
own `.git` file becomes a dangling pointer, and `git worktree repair`
cannot fix it because the registration is gone rather than stale.

The failure is quiet in the worst way. A `git checkout <commit>` in the
broken worktree fails, but a build there still succeeds against whatever
source is on disk, and `--version` reports the commit baked in at the last
*configure* (the git-revision header is generated at configure time, not
build time). So an A/B can silently compare against the wrong baseline
while its banner claims otherwise -- which happened once: a baseline
intended to be pre-H10 was actually pre-H9, so the comparison would have
measured two items and attributed them to one.

What to do:

- After any sync, **recreate the baseline worktree** rather than assuming
  it survived: `git worktree prune && git worktree add -f <dir> <commit>`.
- **Re-run `cmake` configure**, not just `--build`, so the version banner
  matches the checkout.
- **Verify the baseline by content, not only by banner** -- grep for a
  marker of the change under test and confirm it is absent from the
  baseline and present in the new tree. That is the check that caught this.
