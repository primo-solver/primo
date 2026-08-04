#!/usr/bin/env bash
# Sharded stats-mining run over the staged QF_UFLRA tree.
#
# Submit from a prepared remote workspace. Override PRIMO_WORKDIR,
# PRIMO_MINE_OUT_ROOT, PRIMO_MINE_TIMEOUT, PRIMO_MINE_SHARDS, and
# PRIMO_MINE_SOLVER as needed.
#SBATCH --job-name=primo-qfuflra-mine
#SBATCH --array=0-7
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
ROOT=${PRIMO_MINE_ROOT:-tmp/QF_UFLRA}
TIMEOUT=${PRIMO_MINE_TIMEOUT:-2}
SHARDS=${PRIMO_MINE_SHARDS:-8}
OUT_ROOT=${PRIMO_MINE_OUT_ROOT:-"$WORKDIR/results/qf-uflra-model-mine-${SLURM_ARRAY_JOB_ID:-manual}"}
SOLVER=${PRIMO_MINE_SOLVER:-"build-slurm-rel/primo --quiet --stats --model-equality-branching"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

if ((TASK_ID >= SHARDS)); then
  echo "task $TASK_ID is outside the $SHARDS-shard range; exiting"
  exit 0
fi

IFS=' ' read -r -a solver_words <<< "$SOLVER"
solver_binary=${solver_words[0]}
solver_extra_args=("${solver_words[@]:1}")

echo "task $TASK_ID/$SHARDS: solver=${solver_words[*]}"
"$solver_binary" --version || true

python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --root "$ROOT" \
  --solver "$solver_binary" \
  "${solver_extra_args[@]/#/--solver-arg=}" \
  --timeout "$TIMEOUT" \
  --shards "$SHARDS" \
  --shard-index "$TASK_ID" \
  --out "$OUT_ROOT/shard-${TASK_ID}.csv" \
  --summary "$OUT_ROOT/shard-${TASK_ID}-summary.csv" \
  --log-dir "$OUT_ROOT/shard-${TASK_ID}-logs"
