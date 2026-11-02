#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-fallback-qualify
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 scripts/summarize_soi_qualification.py --timing "${PRIMO_TIMING_ROOT:?}" \
 --profiles "${PRIMO_PROFILE_ROOT:?}" --out "${PRIMO_QUALIFICATION_OUT:?}"
