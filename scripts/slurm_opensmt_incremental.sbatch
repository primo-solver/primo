#!/usr/bin/env bash
# Same untouched-stdin driver and resource limits as the recorded native runs.
#SBATCH --job-name=primo-opensmt-incremental
#SBATCH --array=0-17%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
slot=$SLURM_ARRAY_TASK_ID
if (( slot < 16 )); then
  logic=uf
  root=${PRIMO_UF_ROOT:?}
  manifest=${PRIMO_UF_MANIFEST:?}
  shards=16
  shard=$slot
  limit=30
else
  logic=lra
  root=${PRIMO_LRA_ROOT:?}
  manifest=${PRIMO_LRA_MANIFEST:?}
  shards=2
  shard=$((slot - 16))
  limit=600
fi
python3 scripts/native_uf_sessions.py timing \
  --continue-after-error \
  --root "$root" --manifest "$manifest" --solver "${PRIMO_OPENSMT:?}" \
  --solver-arg=--pipe --shards "$shards" --shard-index "$shard" --timeout "$limit" \
  --out "${PRIMO_OPENSMT_OUT:?}/$logic/shard-$shard"
