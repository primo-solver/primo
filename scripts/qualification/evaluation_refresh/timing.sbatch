#!/usr/bin/env bash
# Preserve per-shard hardware identity alongside the common timing driver.
#SBATCH --job-name=primo-refresh-timing
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=03:00:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
mkdir -p "${PRIMO_CAMPAIGN_OUT:?}"
lscpu > "$PRIMO_CAMPAIGN_OUT/hardware-${SLURM_ARRAY_TASK_ID:-0}.txt"
exec bash scripts/slurm_real_corpus_validation.sbatch
