#!/usr/bin/env bash
#SBATCH --job-name=primo-simplex-summary
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --hint=nomultithread
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
artifacts="${PRIMO_SIMPLEX_ARTIFACTS:?}"
export PYTHONPATH="$artifacts/scripts:$PWD/scripts"
python3 "$artifacts/scripts/summarize_lra_simplex_ablation.py" \
 --plan "$artifacts/benchmarks/qf-lra-simplex-ablation/plan.json" \
 --corpus "${PRIMO_SIMPLEX_CORPUS:?}" --results "${PRIMO_SIMPLEX_OUT:?}" \
 --out "$PRIMO_SIMPLEX_OUT/qualified-summary" --shards 64 --verify-artifacts
