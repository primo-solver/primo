#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-fallback-profile
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
read -r -a profile_args <<< "${PRIMO_PROFILE_ARGUMENTS:?}"
"${PRIMO_PROFILE_SOLVER:?}" --version
command -v gdb
python3 scripts/profile_gdb_samples.py --out "${PRIMO_PROFILE_OUT:?}" \
 --timeout 90 --interval 0.05 --seed 611940 -- \
 "$PRIMO_PROFILE_SOLVER" "${profile_args[@]}" "${PRIMO_PROFILE_INPUT:?}"
