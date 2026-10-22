#!/usr/bin/env bash
# Matched CPU allocations; task offsets keep arrays within MaxArraySize=1001.
#SBATCH --job-name=primo-long-limits
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=09:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
exec python3 "${PRIMO_LONG_DRIVER:?}" "${PRIMO_LONG_PHASE:?}" \
  --workspace "$PWD" --out "${PRIMO_LONG_OUT:?}"
