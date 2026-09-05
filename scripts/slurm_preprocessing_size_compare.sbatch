#!/usr/bin/env bash
#SBATCH --job-name=primo-preproc-size
#SBATCH --array=0-39
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Deterministic encoding-size comparison between two Primo builds, using
# scripts/compare_preprocessing_size.py (--preprocess-only, so no search).
#
# Unlike slurm_qf_lia_full_solver_compare.sbatch this is *not* a performance
# run: every number it produces is a pure function of the input and the binary,
# so the two arms can be compared without any argument about timeout-boundary
# churn or TermId order. Each array task runs both solvers over the same shard,
# which keeps the arms in one file and removes the (solver, shard) index
# arithmetic the performance script needs.
#
# --mem is 8G rather than 3G because preprocessing alone peaks high on the
# largest nec-smt files (one is 200KB with 5240 ites in a single assert) and
# there is no search phase to spread the allocation over time.
#
# Submit:
#   PRIMO_SIZE_SOLVERS="base=/path/to/base/primo;head=build-slurm-rel/primo" \
#     sbatch scripts/slurm_preprocessing_size_compare.sbatch

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-${SLURM_SUBMIT_DIR:-$PWD}}
ROOT=${PRIMO_SIZE_ROOT:-sandbox/non-incremental/QF_LIA}
TIMEOUT=${PRIMO_SIZE_TIMEOUT:-60}
SHARDS=${PRIMO_SIZE_SHARDS:-40}
OUT_ROOT=${PRIMO_SIZE_OUT_ROOT:-"$WORKDIR/results/preproc-size-${SLURM_ARRAY_JOB_ID:-manual}"}
SOLVERS=${PRIMO_SIZE_SOLVERS:-"head=build-slurm-rel/primo"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

if [ ! -d "$ROOT" ]; then
  echo "corpus root '$ROOT' not found -- stage it first" >&2
  exit 2
fi

if (( TASK_ID >= SHARDS )); then
  echo "task $TASK_ID is outside the $SHARDS-shard range; exiting"
  exit 0
fi

# Semicolon-separated "name=command", matching the convention in
# slurm_qf_lia_full_solver_compare.sbatch (a space cannot separate them
# because a command may carry its own flags).
solver_args=()
IFS=';' read -r -a solver_specs <<< "$SOLVERS"
for spec in "${solver_specs[@]}"; do
  solver_args+=(--solver "$spec")
  echo "solver spec: $spec"
  # The command may carry its own flags, so take only the binary for the
  # banner check that pins which build this task actually ran.
  IFS=' ' read -r -a solver_words <<< "${spec#*=}"
  "${solver_words[0]}" --version || true
done

python3 scripts/compare_preprocessing_size.py \
  --root "$ROOT" \
  --out "$OUT_ROOT" \
  --timeout "$TIMEOUT" \
  --shards "$SHARDS" \
  --shard-index "$TASK_ID" \
  "${solver_args[@]}"
