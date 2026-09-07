#!/usr/bin/env bash
# Run timing and model replay as distinct jobs over the same sharded corpus.
# Submit replay only after timing jobs finish, with --dependency=afterany:TIMING_ID.
#SBATCH --job-name=primo-real-corpus
#SBATCH --output=slurm_logs/primo-real-corpus-%A_%a.out
#SBATCH --error=slurm_logs/primo-real-corpus-%A_%a.err
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=04:00:00
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
PHASE=${PRIMO_PHASE:?benchmark or replay}
TASK=${SLURM_ARRAY_TASK_ID:-0}
OUT="${PRIMO_CAMPAIGN_OUT:?}/shard-$TASK"
args=("$PHASE" --out "$OUT")
case "$PHASE" in
  benchmark)
    args+=(--root "${PRIMO_CORPUS_ROOT:?}" --solver "${PRIMO_SOLVER:?}"
           --run-id "${PRIMO_CAMPAIGN_ID:?}" --shards "${PRIMO_SHARDS:?}"
           --shard-index "$TASK" --timeout "${PRIMO_TIMEOUT:-60}")
    if [[ -n ${PRIMO_EXPECTED_GIT:-} ]]; then args+=(--expected-git "$PRIMO_EXPECTED_GIT"); fi
    words=()
    if [[ -n ${PRIMO_SOLVER_ARGS:-} ]]; then read -r -a words <<< "$PRIMO_SOLVER_ARGS"; fi
    for arg in "${words[@]}"; do args+=(--solver-arg="$arg"); done
    ;;
  replay)
    args+=(--model-timeout "${PRIMO_MODEL_TIMEOUT:-90}" --dolmen-timeout "${PRIMO_DOLMEN_TIMEOUT:-30}"
           --dolmen-arg=--strict=false --attempt "${PRIMO_REPLAY_ATTEMPT:-model-validation}")
    ;;
  *) echo 'invalid PRIMO_PHASE' >&2; exit 2 ;;
esac
python3 scripts/benchmark_model_replay.py "${args[@]}"
