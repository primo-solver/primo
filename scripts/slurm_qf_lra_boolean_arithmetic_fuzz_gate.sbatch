#!/usr/bin/env bash
# Merge gate for generated QF_LRA formulas mixing Boolean structure with
# arithmetic. Every generated case is answer-checked against Z3, and every
# satisfiable Primo model must pass Dolmen directly; there is no validator
# fallback in this gate.
#SBATCH --job-name=primo-qflra-bool-model-fuzz
#SBATCH --array=0-3
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the remote workspace}
COUNT=${PRIMO_FUZZ_COUNT:-250}
SEED_BASE=${PRIMO_FUZZ_SEED_BASE:-202608310}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/qf-lra-bool-model-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
DOLMEN=${PRIMO_FUZZ_DOLMEN:-dolmen}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"
PATH="$WORKDIR/contrib/local/bin:$HOME/.opam/default/bin:$PATH"
export PATH

for required in build-slurm-rel/primo contrib/local/bin/z3 "$DOLMEN"; do
  if ! command -v "$required" >/dev/null 2>&1 && [[ ! -x $required ]]; then
    echo "required gate tool '$required' not found" >&2
    exit 2
  fi
done

build-slurm-rel/primo --version
python3 scripts/qf_lra_boolean_arithmetic_fuzz.py \
  --solver build-slurm-rel/primo \
  --solver-arg=--quiet \
  --z3 contrib/local/bin/z3 \
  --dolmen "$DOLMEN" \
  --count "$COUNT" \
  --seed "$((SEED_BASE + TASK_ID))" \
  --timeout 5 \
  --dolmen-timeout 10 \
  --out "$OUT_ROOT/task-$TASK_ID"
