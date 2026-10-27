#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-diagnose
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=7G
#SBATCH --time=00:06:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
exec python3 "${PRIMO_WORKFLOW:?}/diagnose.py" "${PRIMO_DIAG_MODE:?}"
