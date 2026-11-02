#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-fallback-summary
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 scripts/summarize_soi_improvements.py --run "${PRIMO_SUMMARY_RUN:?}" \
 --historical docs/coverage/lra-simplex-per-input-20261031.csv \
 --baseline original-historical-g1-f0 --baseline eager-historical-g1-f0 \
 --baseline original-combined-g1-f0 --baseline eager-combined-g1-f0 \
 --baseline eager-historical-g1-f1 --baseline eager-combined-g1-f1 \
 --out "${PRIMO_SUMMARY_OUT:?}"
