#!/usr/bin/env bash
# Reusable SLURM gate for QF_UFLIA answer fuzzing and direct model validation.
#
# Submit from a workspace prepared by slurm_prepare_benchmark_workspace.py.
# PRIMO_FUZZ_SOLVER_ARGS is split into option tokens with no embedded spaces.
# Keep cluster-specific paths and identifiers out of this file.
#SBATCH --job-name=primo-qf-uflia-model-fuzz
#SBATCH --output=slurm_logs/primo-qf-uflia-model-fuzz-%A_%a.out
#SBATCH --error=slurm_logs/primo-qf-uflia-model-fuzz-%A_%a.err
#SBATCH --array=0-3
#SBATCH --time=00:30:00
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the prepared remote workspace}
COUNT=${PRIMO_FUZZ_COUNT:-750}
SEED_BASE=${PRIMO_FUZZ_SEED_BASE:-202609010}
SOLVER_ARGS_TEXT=${PRIMO_FUZZ_SOLVER_ARGS:-}
# "mixed" spends a third of its cases on composed terms, which is what
# reaches nested applications; "random" is the older fixed-vocabulary shape
# whose UF nesting never exceeds one.
SHAPE=${PRIMO_FUZZ_SHAPE:-mixed}
DEPTH=${PRIMO_FUZZ_DEPTH:-3}
OUT_ROOT=${PRIMO_FUZZ_OUT_DIR:-"$WORKDIR/fuzz-results/qf-uflia-model-gate-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
SEED=$((SEED_BASE + TASK_ID))
OUT="$OUT_ROOT/task-$TASK_ID"

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT"
PATH="$WORKDIR/contrib/local/bin:$PATH"
export PATH

required_tools=(z3 dolmen)
for required in "${required_tools[@]}"; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required gate tool '$required' not found" >&2
    exit 2
  fi
done

solver_args=()
model_solver_args=(--solver-arg=--quiet)
if [[ -n $SOLVER_ARGS_TEXT ]]; then
  read -r -a solver_arg_words <<< "$SOLVER_ARGS_TEXT"
  for arg in "${solver_arg_words[@]}"; do
    solver_args+=(--solver-arg="$arg")
    model_solver_args+=(--solver-arg="$arg")
  done
fi

./build-slurm-rel/primo --version
python3 scripts/qf_uflia_fuzz.py \
  --solver ./build-slurm-rel/primo \
  --solver-arg=--quiet \
  "${solver_args[@]}" \
  --ref z3 \
  --count "$COUNT" \
  --seed "$SEED" \
  --timeout 5 \
  --shape "$SHAPE" \
  --depth "$DEPTH" \
  --out "$OUT/answer"

python3 scripts/validate_model.py \
  --solver ./build-slurm-rel/primo \
  "${model_solver_args[@]}" \
  --require-dolmen-pass \
  --timeout 5 \
  --dolmen-timeout 5 \
  --dolmen-arg=--strict=false \
  --out "$OUT/model-validation" \
  "$OUT"/answer/cases/*.smt2
