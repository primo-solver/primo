#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-uf-extra
#SBATCH --array=0-7%4
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:20:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
[[ -n ${SLURM_JOB_ID:-} ]]
task=$SLURM_ARRAY_TASK_ID
mode=generated
count=1000
shard=$task
extra=()
if (( task >= 4 )); then
  mode=mutations
  count=200
  shard=$((task-4))
  extra+=(--corpus "${PRIMO_UF_CORPUS:?}")
fi
python3 scripts/quantifier_preparation_gate.py --mode "$mode" --count "$count" \
  --shards 4 --shard-index "$shard" --seed 20260925 --timeout 5 \
  --solver build-slurm-rel/primo --expected-git "${PRIMO_EXPECTED_GIT:?}" \
  --ref contrib/local/bin/z3 --ref contrib/local/bin/cvc5 \
  --out "${PRIMO_UF_EXTRA_OUT:?}/$mode-$shard" "${extra[@]}"
