#!/usr/bin/env bash
# SLURM mutation gate for mixed QF_UFLRA real-seed fuzzing.
#
# This gate assumes the remote workspace was prepared on the login node with:
#   make contrib-yinyang contrib-z3
# It does not install Python packages from compute nodes.

#SBATCH --job-name=primo-qfuflra-mut
#SBATCH --array=0-1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=08:00:00
#SBATCH --output=slurm_logs/%x-%A-%a.out
#SBATCH --error=slurm_logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the fresh remote workspace}
SOLVER_BIN=${PRIMO_FUZZ_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/qf-uflra-mutation-gate-${SLURM_ARRAY_JOB_ID:-manual}"}
YINYANG_SEEDS=${PRIMO_FUZZ_YINYANG_SEEDS:-"$WORKDIR/benchmarks/seeds/qf_uflra"}
YINYANG_ITERATIONS=${PRIMO_FUZZ_YINYANG_ITERATIONS:-200}
YINYANG_TIMEOUT=${PRIMO_FUZZ_YINYANG_TIMEOUT:-8}
YINYANG_WALL_SECONDS=${PRIMO_FUZZ_YINYANG_WALL_SECONDS:-27000}
FILE_SIZE_LIMIT=${PRIMO_FUZZ_FILE_SIZE_LIMIT:-200000}
MUTATION_REF=${PRIMO_FUZZ_MUTATION_REF:-"z3 model_validate=true"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"

PATH="$WORKDIR/contrib/local/bin:$PATH"
if [ -d "$WORKDIR/.venv-yinyang/bin" ]; then
  PATH="$WORKDIR/.venv-yinyang/bin:$PATH"
fi
export PATH

for required in z3 typefuzz; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required tool '$required' not found on PATH; run make contrib-yinyang contrib-z3 before sbatch" >&2
    exit 2
  fi
done

if [[ ! -e "$YINYANG_SEEDS" ]]; then
  echo "QF_UFLRA mutation seed path does not exist: $YINYANG_SEEDS" >&2
  exit 1
fi

IFS=' ' read -r -a solver_words <<< "$SOLVER_BIN"
"${solver_words[0]}" --version
z3 --version

if (( TASK_ID == 0 )); then
  label=default
  option_args=()
elif (( TASK_ID == 1 )); then
  label=candidate-final
  option_args=(--qf-uflra-candidate-final-scan)
else
  echo "task $TASK_ID is outside the active 2-task range"
  exit 0
fi

out="$OUT_ROOT/$label"
mkdir -p "$out"
solver_command="$SOLVER_BIN --quiet"
for arg in "${option_args[@]}"; do
  solver_command+=" $arg"
done

echo "task $TASK_ID: QF_UFLRA $label typefuzz over $YINYANG_SEEDS"
scripts/yinyang_fuzz.sh \
  --seeds "$YINYANG_SEEDS" \
  --solver "$solver_command" \
  --ref "$MUTATION_REF" \
  --seconds "$YINYANG_WALL_SECONDS" \
  --iterations "$YINYANG_ITERATIONS" \
  --solver-timeout "$YINYANG_TIMEOUT" \
  --bugs "$out/bugs" \
  --scratch "$out/scratch" \
  --logs "$out/logs" \
  --file-size-limit "$FILE_SIZE_LIMIT" \
  --keep-logic \
  --hoist-declarations
