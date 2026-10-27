#!/usr/bin/env bash
# Replay candidate SAT results from completed loss-rerun shards, after timing.
#SBATCH --job-name=primo-integer-loss-models
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=00:20:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
mapfile -t parents < "${PRIMO_REPLAY_TASKS:?}"
TASK=${SLURM_ARRAY_TASK_ID:-0}
(( TASK < ${#parents[@]} )) || exit 2
python3 scripts/benchmark_model_replay.py replay \
  --out "${parents[$TASK]}" \
  --dolmen "${PRIMO_DOLMEN:-$PWD/contrib/local/bin/dolmen}" \
  --model-timeout 180 --dolmen-timeout 60 --dolmen-size 8G \
  --dolmen-arg=--strict=false \
  --attempt "${PRIMO_MODEL_ATTEMPT:-model-validation}"
