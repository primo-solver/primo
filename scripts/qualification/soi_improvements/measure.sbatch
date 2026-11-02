#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-measure
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=04:00:00
#SBATCH --hint=nomultithread
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
extra=()
if [[ ${PRIMO_SOI_PHASE:?} == models ]]; then
  extra+=(--dolmen "${PRIMO_DOLMEN:?}")
fi
python3 scripts/run_soi_improvements.py "$PRIMO_SOI_PHASE" \
  --plan "${PRIMO_SOI_PLAN:?}" --corpus "${PRIMO_SOI_CORPUS:?}" \
  --solver "${PRIMO_SOI_SOLVER:?}" --out "${PRIMO_SOI_OUT:?}" \
  --shards "${PRIMO_SOI_SHARDS:?}" --shard "${SLURM_ARRAY_TASK_ID:?}" "${extra[@]}"
