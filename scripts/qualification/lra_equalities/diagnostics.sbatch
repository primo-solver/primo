#!/usr/bin/env bash
# Separate diagnostic allocation; these are not headline timings.
#SBATCH --job-name=primo-lra-equality-diagnostics
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=7G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
exec python3 "${PRIMO_DIAGNOSTIC_DRIVER:?}" "${PRIMO_DIAG_MODE:?}"
