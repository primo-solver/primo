#!/usr/bin/env bash
#SBATCH --job-name=primo-qflra-scrambled-compare
#SBATCH --array=0-59
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Primo vs reference solvers over the scrambled QF_LRA competition set
# (sandbox/QF_LRA, 595 files). Separate from
# slurm_qf_lra_solver_compare.sbatch because this set needs a different shape:
#
#  * It is much larger per file -- median 129KB, p95 9.3MB, max 62.5MB against
#    the stratified sample's much smaller files. Primo peaks at ~2.4GB RSS on
#    the 62MB instance, so --mem is 12G here rather than 3G.
#  * 595 files at a useful timeout does not fit one task per solver, so this
#    shards the manifest as well: task -> (solver, shard), giving
#    SOLVERS x SHARDS array tasks. The per-solver CSVs are then merged by
#    scripts/merge_shard_csvs.py before analysis.
#  * The files are *scrambled*, so they carry no (set-info :status). That
#    means bench_qf_uf_full.py's own wrong-answer check is inert here: it
#    derives `expected` from :status and reports wrong=false when there is
#    none. Disagreement has to be found by comparing the per-solver CSVs
#    against each other, which scripts/compare_solver_answers.py does.
#    Do not read a "0 wrong" summary from this set as evidence of soundness
#    on its own.
#
# Validate before submitting:
#   python3 scripts/slurm_validate_sbatch.py scripts/slurm_qf_lra_scrambled_compare.sbatch
#
# Submit:
#   sbatch scripts/slurm_qf_lra_scrambled_compare.sbatch
#
# If you change SHARDS or the solver list, pass a matching
# --array=0-(SOLVERS*SHARDS-1); SBATCH directives are static and cannot read
# env vars.

set -euo pipefail

# Default to the directory the job was submitted from, so two parallel
# work streams in two checkouts cannot land in the same tree. A shared
# hardcoded default was how a QF_LRA sync nearly clobbered the binary a
# running Nelson-Oppen job depended on (2026-07-31).
WORKDIR=${PRIMO_WORKDIR:-${SLURM_SUBMIT_DIR:-$PWD}}
ROOT=${PRIMO_COMPARE_ROOT:-sandbox/QF_LRA}
MANIFEST=${PRIMO_COMPARE_MANIFEST:-benchmarks/qf-lra-scrambled-competition.txt}
TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-60}
SHARDS=${PRIMO_COMPARE_SHARDS:-20}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/qf-lra-scrambled-${SLURM_ARRAY_JOB_ID:-manual}"}
# Semicolon-separated "name=command"; the command may contain its own spaces
# and flags (see slurm_qf_lra_solver_compare.sbatch's own note on why the
# separator is a semicolon and not a space).
SOLVERS=${PRIMO_COMPARE_SOLVERS:-"primo=build-slurm-rel/primo --quiet;z3=contrib/local/bin/z3;cvc5=contrib/local/bin/cvc5"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

IFS=';' read -r -a solver_specs <<< "$SOLVERS"
total_tasks=$(( ${#solver_specs[@]} * SHARDS ))
if (( TASK_ID >= total_tasks )); then
  echo "task $TASK_ID is outside the active $total_tasks-task range; exiting"
  exit 0
fi

solver_index=$(( TASK_ID / SHARDS ))
shard_index=$(( TASK_ID % SHARDS ))

spec=${solver_specs[$solver_index]}
name=${spec%%=*}
command_str=${spec#*=}
if [[ -z "$name" || "$name" == "$spec" ]]; then
  echo "invalid solver spec '$spec'; expected name=command" >&2
  exit 2
fi
IFS=' ' read -r -a solver_words <<< "$command_str"
solver_binary=${solver_words[0]}
solver_extra_args=("${solver_words[@]:1}")

echo "task $TASK_ID: solver=$name shard=$shard_index/$SHARDS command=${solver_words[*]}"
"$solver_binary" --version 2>&1 | head -3 || true

# --solver takes only the base binary; extra flags go through --solver-arg,
# because bench_qf_uf_full.py's --solver uses argparse nargs="+" and stops
# consuming at the first token starting with "-". Same convention as
# slurm_qf_lra_solver_compare.sbatch, where that gotcha was originally found.
python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --root "$ROOT" \
  --manifest "$MANIFEST" \
  --solver "$solver_binary" \
  "${solver_extra_args[@]/#/--solver-arg=}" \
  --timeout "$TIMEOUT" \
  --shards "$SHARDS" \
  --shard-index "$shard_index" \
  --out "$OUT_ROOT/$name.shard-$shard_index.csv" \
  --summary "$OUT_ROOT/$name.shard-$shard_index-summary.csv"
