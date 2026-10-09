#!/usr/bin/env bash
# Separate SAT-model validation with smaller reservations on compute nodes.
# Timing provenance supplies the immutable solver, arguments and input hashes.
# Array tasks encode profile_index * PRIMO_SHARDS + shard_index.
#SBATCH --job-name=primo-lra-selected-models
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --partition=compute
#SBATCH --mem=6G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
shards=${PRIMO_SHARDS:?}
task=${SLURM_ARRAY_TASK_ID:?}
case $((task / shards)) in
  0) profile=default ;;
  1) profile=model-phase ;;
  2) profile=monotone ;;
  3) profile=both ;;
  *) echo 'model replay accepts only the four native profiles' >&2; exit 2 ;;
esac
export PRIMO_PHASE=replay
export PRIMO_DOLMEN_SIZE=${PRIMO_DOLMEN_SIZE:-5G}
export PRIMO_CAMPAIGN_OUT="${PRIMO_CAMPAIGN_OUT:?}/$profile"
export SLURM_ARRAY_TASK_ID=$((task % shards))
exec bash scripts/slurm_real_corpus_validation.sbatch
