#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-case-study-qualify
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --hint=nomultithread
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PYTHONPATH="$PWD/scripts"
python3 "${PRIMO_CASE_ARTIFACTS:?}/qualify_lra_case_study.py" \
 --corpus "${PRIMO_CASE_CORPUS:?}" --results "${PRIMO_CASE_RESULTS:?}" \
 --out "$PRIMO_CASE_CORPUS/metadata.json"
