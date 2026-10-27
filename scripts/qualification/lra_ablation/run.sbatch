#!/usr/bin/env bash
# Four default-off QF_LRA profiles; timing and model replay are separate jobs.
# Gate tasks interleave profiles: task = gate_shard * 4 + profile.
# Timing/replay tasks use the same interleaving with PRIMO_SHARDS shards.
#SBATCH --job-name=primo-lra-ablation
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=7G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PWD/.venv-yinyang/bin:$PATH"
exec python3 scripts/qualification/lra_ablation/run.py "${PRIMO_PHASE:?}"
