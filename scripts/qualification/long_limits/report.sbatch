#!/usr/bin/env bash
# Metadata audits run after the entire corresponding campaign phase.
#SBATCH --job-name=primo-long-audit
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
# SLURM reservations are not a verified hard cap on this cluster.
# Bash applies this virtual-address-space limit to both soft and hard RLIMIT_AS.
ulimit -v 8388608
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
exec python3 "${PRIMO_LONG_REPORT:?}" "${PRIMO_LONG_STAGE:?}" --workspace "$PWD" --out "${PRIMO_LONG_OUT:?}"
