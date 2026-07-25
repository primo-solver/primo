#!/usr/bin/env bash
#SBATCH --job-name=primo-html334-raw-amo
#SBATCH --output=results/html334-raw-amo-2905733/logs/%A_%a.out
#SBATCH --error=results/html334-raw-amo-2905733/logs/%A_%a.err
#SBATCH --array=0-15
#SBATCH --time=02:30:00
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$HOME/primo-raw-amo}
RESULT_DIR="$WORKDIR/results/html334-raw-amo-2905733"
SHARDS=16
SHARD_INDEX="${SLURM_ARRAY_TASK_ID:-0}"

cd "$WORKDIR"
mkdir -p "$RESULT_DIR/logs" "$RESULT_DIR/shards"

python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --root tmp/QF_UF \
  --manifest "$RESULT_DIR/manifest.txt" \
  --solver "$WORKDIR/build-slurm-rel/primo" \
  --solver-arg=--quiet \
  --solver-arg=--stats \
  --timeout 120 \
  --shards "$SHARDS" \
  --shard-index "$SHARD_INDEX" \
  --out "$RESULT_DIR/shards/shard-$SHARD_INDEX.csv" \
  --summary "$RESULT_DIR/shards/shard-$SHARD_INDEX-summary.csv" \
  --log-dir "$RESULT_DIR/logs"
