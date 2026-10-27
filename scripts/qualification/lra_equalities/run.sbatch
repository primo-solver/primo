#!/usr/bin/env bash
# Same-binary default/equality/both/both-equality arms; separate timing/replay.
#SBATCH --job-name=primo-lra-equalities
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=7G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PWD/.venv-yinyang/bin:$PATH"
exec python3 scripts/qualification/lra_equalities/campaign.py "${PRIMO_PHASE:?}"
