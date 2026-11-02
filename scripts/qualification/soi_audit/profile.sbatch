#!/usr/bin/env bash
# Diagnostic instruction profiles; never use instrumented wall time as a speed result.
#SBATCH --job-name=primo-soi-audit
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:08:00
#SBATCH --hint=nomultithread
set -euo pipefail
export DEBUGINFOD_URLS=
cd "${PRIMO_WORKDIR:?}"
python3 "${PRIMO_SOI_ARTIFACTS:?}/profile.py" \
  --solver "$PWD/build-slurm-rel/primo" \
  --corpus "${PRIMO_SOI_CORPUS:?}" --out "${PRIMO_SOI_OUT:?}" \
  --case "${SLURM_ARRAY_TASK_ID:?}"
