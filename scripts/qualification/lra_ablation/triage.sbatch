#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-mutation-triage
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
exec python3 "${PRIMO_CAMPAIGN_OUT:?}/workflow/triage.py" "$PRIMO_CAMPAIGN_OUT" \
  --shard "${SLURM_ARRAY_TASK_ID:?}"
