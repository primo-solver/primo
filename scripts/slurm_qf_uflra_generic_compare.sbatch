#!/usr/bin/env bash
# Paired full-corpus comparison of QF_UFLRA's specialized default coordinator
# and the experimental generic model-combination coordinator. Each task runs
# both arms over one shard on the same allocated node. The arm order alternates
# by shard to reduce systematic warm-cache and cluster-load bias.
#SBATCH --job-name=primo-qfuflra-generic-ab
#SBATCH --array=0-15%8
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=04:00:00
#SBATCH --output=slurm_logs/%x-%A-%a.out
#SBATCH --error=slurm_logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
ROOT=${PRIMO_COMPARE_ROOT:-tmp/QF_UFLRA}
TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-120}
SHARDS=${PRIMO_COMPARE_SHARDS:-16}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/qf-uflra-generic-ab-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
SOLVER=${PRIMO_COMPARE_SOLVER:-build-slurm-rel/primo}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"

if ((TASK_ID >= SHARDS)); then
  echo "task $TASK_ID is outside the $SHARDS-shard range; exiting"
  exit 0
fi

"$SOLVER" --version

run_arm() {
  local name=$1
  shift
  echo "task $TASK_ID/$SHARDS: arm=$name options=$*"
  python3 scripts/bench_qf_uf_full.py \
    --repo "$WORKDIR" \
    --root "$ROOT" \
    --solver "$SOLVER" \
    --solver-arg=--quiet \
    --solver-arg=--stats \
    "$@" \
    --timeout "$TIMEOUT" \
    --shards "$SHARDS" \
    --shard-index "$TASK_ID" \
    --out "$OUT_ROOT/$name/shard-${TASK_ID}.csv" \
    --summary "$OUT_ROOT/$name/shard-${TASK_ID}-summary.csv" \
    --log-dir "$OUT_ROOT/$name/shard-${TASK_ID}-logs"
}

if ((TASK_ID % 2 == 0)); then
  run_arm specialized
  run_arm generic --solver-arg=--experimental-generic-model-combination
else
  run_arm generic --solver-arg=--experimental-generic-model-combination
  run_arm specialized
fi
