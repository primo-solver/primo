#!/usr/bin/env bash
#SBATCH --job-name=primo-integer-model-audit
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:20:00
#SBATCH --output=slurm_logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 results/audit_models.py --results results --out results/integer-model-audit-reviewed/part-${SLURM_ARRAY_TASK_ID:?} --part "$SLURM_ARRAY_TASK_ID" --parts 16
