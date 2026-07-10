#!/usr/bin/env bash
#SBATCH --job-name=primo-interesting-qfuf
#SBATCH --array=0-31
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:45:00
#SBATCH --output=slurm_logs/interesting-qfuf-%A-%a.out
#SBATCH --error=slurm_logs/interesting-qfuf-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_SLURM_WORKDIR:-"$PWD"}
PRIMO_BIN=${PRIMO_BIN:-"$WORKDIR/build/primo"}
MANIFEST=${PRIMO_INTERESTING_MANIFEST:-"$WORKDIR/benchmarks/interesting-qf-uf.txt"}
OUT_DIR=${PRIMO_INTERESTING_OUT_DIR:-"$WORKDIR/results/interesting-qf-uf-slurm"}
TIMEOUT=${PRIMO_SOLVER_TIMEOUT:-30}
SHARDS=${PRIMO_INTERESTING_SHARDS:-32}
SHARD_INDEX=${SLURM_ARRAY_TASK_ID:-0}

mkdir -p "$OUT_DIR/logs/$SHARD_INDEX" "$WORKDIR/slurm_logs"
cd "$WORKDIR"

python3 scripts/bench_interesting_qf_uf.py \
  --solver "$PRIMO_BIN" \
  --solver-arg=--quiet \
  --solver-arg=--stats \
  --solver-arg=--qf-uf-profile=tuned-interesting \
  --solver-arg=--euf-prop=model \
  --timeout "$TIMEOUT" \
  --manifest "$MANIFEST" \
  --shards "$SHARDS" \
  --shard-index "$SHARD_INDEX" \
  --out "$OUT_DIR/shard-$SHARD_INDEX.csv" \
  --summary "$OUT_DIR/shard-$SHARD_INDEX-summary.csv" \
  --log-dir "$OUT_DIR/logs/$SHARD_INDEX"
