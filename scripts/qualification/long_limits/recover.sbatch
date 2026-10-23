#!/usr/bin/env bash
# Reproduce the actual external crash and verify the following input still runs.
#SBATCH --job-name=primo-long-recovery-smoke
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
exec python3 "${PRIMO_LONG_RECOVER:?}" smoke --workspace "$PWD" --out "${PRIMO_LONG_OUT:?}"
