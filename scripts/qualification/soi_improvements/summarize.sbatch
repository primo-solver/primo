#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-summary
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=00:30:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
args=()
while IFS= read -r run; do
  args+=(--run "$run")
done < "${PRIMO_SOI_RUNS_FILE:?}"
read -r -a baselines <<< "${PRIMO_SOI_BASELINES:?}"
for baseline in "${baselines[@]}"; do
  args+=(--baseline "$baseline")
done
python3 scripts/summarize_soi_improvements.py "${args[@]}" \
  --historical docs/coverage/lra-simplex-per-input-20261031.csv \
  --out "${PRIMO_SOI_SUMMARY:?}"
