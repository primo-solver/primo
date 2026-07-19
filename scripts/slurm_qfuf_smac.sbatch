#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuf-smac
#SBATCH --time=06:00:00
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-/home/janotmik/primo-cadical-profile}
SOLVER=${PRIMO_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
TRAIN=${PRIMO_SMAC_TRAIN:-benchmarks/qfuf-smac-gap-train.txt}
VALIDATE=${PRIMO_SMAC_VALIDATE:-benchmarks/qfuf-smac-gap-validate.txt}
GUARD=${PRIMO_SMAC_GUARD:-}
TRIALS=${PRIMO_SMAC_TRIALS:-80}
TIMEOUT=${PRIMO_SMAC_TIMEOUT:-45}
VALIDATION_TIMEOUT=${PRIMO_SMAC_VALIDATION_TIMEOUT:-120}
GUARD_TIMEOUT=${PRIMO_SMAC_GUARD_TIMEOUT:-$VALIDATION_TIMEOUT}
SEED=${PRIMO_SMAC_SEED:-20260718}
LABEL=${PRIMO_SMAC_LABEL:-qfuf-gap}
JOB_ID=${SLURM_JOB_ID:-manual}
OUT_DIR=${PRIMO_SMAC_OUT_DIR:-"results/smac/${LABEL}-${JOB_ID}"}

cd "$WORKDIR"
mkdir -p logs "$OUT_DIR"

if [ ! -x .venv-smac/bin/python ]; then
  python3 -m venv .venv-smac
  .venv-smac/bin/python -m pip install --upgrade pip
  .venv-smac/bin/python -m pip install 'smac>=2,<3'
fi

"$SOLVER" --version
.venv-smac/bin/python - <<'PY'
import ConfigSpace
import smac
print("ConfigSpace", getattr(ConfigSpace, "__version__", "unknown"))
print("smac", getattr(smac, "__version__", "unknown"))
PY

EXTRA_ARGS=()
if [ -n "$GUARD" ]; then
  EXTRA_ARGS+=(--guard "$GUARD" --guard-timeout "$GUARD_TIMEOUT")
fi

.venv-smac/bin/python scripts/smac/qfuf_gap_tune.py \
  --repo "$WORKDIR" \
  --solver "$SOLVER" \
  --train "$TRAIN" \
  --validate "$VALIDATE" \
  --out-dir "$OUT_DIR" \
  --timeout "$TIMEOUT" \
  --validation-timeout "$VALIDATION_TIMEOUT" \
  --trials "$TRIALS" \
  --seed "$SEED" \
  "${EXTRA_ARGS[@]}"
