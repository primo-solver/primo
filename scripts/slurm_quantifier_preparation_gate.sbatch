#!/usr/bin/env bash
# Fresh generated, native metamorphic, and arithmetic preparation gates.
#SBATCH --job-name=primo-quant-preparation
#SBATCH --array=0-59%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
task=${SLURM_ARRAY_TASK_ID:?}
root=${PRIMO_GATE_OUT:?}
args=()
if (( task < 30 )); then
  mode=generated
  count=3000
  shards=30
  shard=$task
elif (( task < 50 )); then
  mode=mutations
  count=1000
  shards=20
  shard=$((task - 30))
  args+=(--corpus "${PRIMO_UF_CORPUS:?}")
else
  mode=arithmetic
  count=1000
  shards=10
  shard=$((task - 50))
fi
python3 scripts/quantifier_preparation_gate.py \
  --mode "$mode" --count "$count" --shards "$shards" --shard-index "$shard" \
  --solver build-slurm-rel/primo --ref contrib/local/bin/z3 \
  --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
  --ref contrib/local/bin/cvc5 --seed 20260914 --timeout 5 \
  --out "$root/$mode-$shard" "${args[@]}"
