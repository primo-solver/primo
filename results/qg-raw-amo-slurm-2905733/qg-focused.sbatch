#!/usr/bin/env bash
#SBATCH --job-name=primo-qg-raw-amo
#SBATCH --output=results/qg-raw-amo-slurm-2905733/logs/%A_%a.out
#SBATCH --error=results/qg-raw-amo-slurm-2905733/logs/%A_%a.err
#SBATCH --array=0-7
#SBATCH --time=02:30:00
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$HOME/primo-raw-amo}
RESULT_DIR="$WORKDIR/results/qg-raw-amo-slurm-2905733"
SHARDS=8
SHARD_INDEX="${SLURM_ARRAY_TASK_ID:-0}"

cd "$WORKDIR"
mkdir -p "$RESULT_DIR/logs" "$RESULT_DIR/shards"

python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --root tmp/QF_UF \
  --manifest "$RESULT_DIR/target-manifest.txt" \
  --solver "$WORKDIR/build-slurm-rel/primo" \
  --solver-arg=--quiet \
  --solver-arg=--stats \
  --timeout 120 \
  --shards "$SHARDS" \
  --shard-index "$SHARD_INDEX" \
  --out "$RESULT_DIR/shards/shard-$SHARD_INDEX.csv" \
  --summary "$RESULT_DIR/shards/shard-$SHARD_INDEX-summary.csv" \
  --log-dir "$RESULT_DIR/logs"
