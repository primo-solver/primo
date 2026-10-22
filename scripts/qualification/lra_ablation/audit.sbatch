#!/usr/bin/env bash
# Collect complete arrays before allowing dependent measurements or qualification.
#SBATCH --job-name=primo-lra-ablation-audit
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=2G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A.out
#SBATCH --error=logs/%x-%A.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
args=("${PRIMO_CAMPAIGN_OUT:?}" --expected "${PRIMO_EXPECTED_INPUTS:?}"
      --shards "${PRIMO_SHARDS:?}" --timeout "${PRIMO_TIMEOUT:?}"
      --out "$PRIMO_CAMPAIGN_OUT/${PRIMO_AUDIT_NAME:?}.json")
if [[ ${PRIMO_REQUIRE_MODELS:-false} == true ]]; then
  args+=(--require-models)
fi
exec python3 "${PRIMO_WORKFLOW:?}/summarize.py" "${args[@]}"
