#!/usr/bin/env bash
# Six-logic assignment migration gate, retained sessions and real UF mutation.
#SBATCH --job-name=primo-shared-gate
#SBATCH --array=0-17%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:50:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 scripts/shared_assignment_gate.py \
  --task "${SLURM_ARRAY_TASK_ID:?}" --expected-git "${PRIMO_EXPECTED_GIT:?}" \
  --out "${PRIMO_GATE_OUT:?}" --uf-seeds "${PRIMO_UF_SEEDS:?}"
