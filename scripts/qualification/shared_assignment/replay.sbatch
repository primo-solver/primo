#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-replay
#SBATCH --array=0-3%4
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
status=0
for profile in default online lazy-uf offline-uflia; do
 for round in 0 1; do
  parent=${PRIMO_COMPARE_OUT_ROOT:?}/$profile/round-$round/candidate/shard-$SLURM_ARRAY_TASK_ID
  python3 scripts/benchmark_model_replay.py replay --out "$parent" \
   --dolmen contrib/local/bin/dolmen --dolmen-arg=--strict=false \
   --model-timeout 30 --dolmen-timeout 30 --attempt full-coverage || status=$?
 done
done
exit "$status"
