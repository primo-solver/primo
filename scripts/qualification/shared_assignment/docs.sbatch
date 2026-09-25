#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-docs
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
[[ -n ${SLURM_JOB_ID:-} ]]
make docs
make docs-next
