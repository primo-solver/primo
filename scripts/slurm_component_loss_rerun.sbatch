#!/usr/bin/env bash
# Three paired repetitions of an explicit lost-solve manifest, no references.
# A loss in at least two pairs blocks migration acceptance; retain all outcomes.
#SBATCH --job-name=primo-component-loss
#SBATCH --array=0
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
TASK=${SLURM_ARRAY_TASK_ID:-0}
SHARDS=${PRIMO_COMPARE_SHARDS:-1}
OUT=${PRIMO_COMPARE_OUT_ROOT:?}
ROOT=${PRIMO_COMPARE_ROOT:?}
MANIFEST=${PRIMO_COMPARE_MANIFEST:?}
BASELINE=${PRIMO_BASELINE_SOLVER:?}
CANDIDATE=${PRIMO_CANDIDATE_SOLVER:?}
BASELINE_GIT=${PRIMO_BASELINE_GIT:?}
CANDIDATE_GIT=${PRIMO_EXPECTED_GIT:?}
(( TASK < SHARDS )) || exit 2
extra=()
words=()
if [[ -n ${PRIMO_SOLVER_ARGS:-} ]]; then read -r -a words <<< "$PRIMO_SOLVER_ARGS"; fi
for arg in "${words[@]}"; do extra+=(--solver-arg="$arg"); done
mkdir -p "$OUT"
hostname > "$OUT/node-$TASK.txt"
for round in 0 1 2; do
  arms=(baseline candidate)
  if (( (TASK + round) % 2 )); then arms=(candidate baseline); fi
  for arm in "${arms[@]}"; do
    binary=$BASELINE
    revision=$BASELINE_GIT
    if [[ $arm == candidate ]]; then binary=$CANDIDATE; revision=$CANDIDATE_GIT; fi
    python3 scripts/benchmark_model_replay.py benchmark \
      --root "$ROOT" --manifest "$MANIFEST" --solver "$binary" --expected-git "$revision" \
      --solver-arg=--quiet --solver-arg=--stats "${extra[@]}" \
      --run-id "component-loss-${SLURM_ARRAY_JOB_ID:-manual}-$round-$arm-$TASK" \
      --shards "$SHARDS" --shard-index "$TASK" --timeout "${PRIMO_COMPARE_TIMEOUT:-60}" \
      --out "$OUT/round-$round/$arm/shard-$TASK"
  done
done
