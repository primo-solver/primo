#!/usr/bin/env bash
# External baseline/candidate comparison; no reference solvers are launched.
# Each allocation runs both binaries twice, reversing the paired arm order.
# Model replay is a separate job and must not be included in these timings.
#SBATCH --job-name=primo-shared-ab
#SBATCH --array=0-3%4
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
TASK=${SLURM_ARRAY_TASK_ID:-0}
SHARDS=${PRIMO_COMPARE_SHARDS:-4}
OUT=${PRIMO_COMPARE_OUT_ROOT:?}
ROOT=${PRIMO_COMPARE_ROOT:?}
BASELINE=${PRIMO_BASELINE_SOLVER:?}
CANDIDATE=${PRIMO_CANDIDATE_SOLVER:?}
BASELINE_GIT=${PRIMO_BASELINE_GIT:?}
CANDIDATE_GIT=${PRIMO_EXPECTED_GIT:?}
(( TASK < SHARDS )) || exit 2
extra=()
if [[ -n ${PRIMO_COMPARE_MANIFEST:-} ]]; then
  extra+=(--manifest "$PRIMO_COMPARE_MANIFEST")
fi
words=()
if [[ -n ${PRIMO_SOLVER_ARGS:-} ]]; then read -r -a words <<< "$PRIMO_SOLVER_ARGS"; fi
for arg in "${words[@]}"; do extra+=(--solver-arg="$arg"); done
mkdir -p "$OUT"
hostname > "$OUT/node-$TASK.txt"
for round in 0 1; do
  arms=(baseline candidate)
  if (( (TASK + round) % 2 )); then arms=(candidate baseline); fi
  for arm in "${arms[@]}"; do
    binary=$BASELINE
    revision=$BASELINE_GIT
    if [[ $arm == candidate ]]; then binary=$CANDIDATE; revision=$CANDIDATE_GIT; fi
    mkdir -p "$OUT/round-$round/$arm" "$OUT/rss"
    python3 scripts/qualification/shared_assignment/record_rss.py "$OUT/rss/round-$round-$arm-$TASK.txt" \
      python3 scripts/benchmark_model_replay.py benchmark \
      --root "$ROOT" --solver "$binary" --expected-git "$revision" \
      --solver-arg=--quiet --solver-arg=--stats "${extra[@]}" \
      --run-id "component-ab-${SLURM_ARRAY_JOB_ID:-manual}-$round-$arm-$TASK" \
      --shards "$SHARDS" --shard-index "$TASK" --timeout "${PRIMO_COMPARE_TIMEOUT:-60}" \
      --out "$OUT/round-$round/$arm/shard-$TASK"
  done
done
