#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuf-phase
#SBATCH --array=0-31
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=01:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
SOLVER=${PRIMO_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
OUT_ROOT=${PRIMO_PHASE_OUT_ROOT:-"$WORKDIR/results/qfuf-phase-compare-${SLURM_ARRAY_JOB_ID:-manual}"}
TIMEOUT=${PRIMO_PHASE_TIMEOUT:-60}
SHARDS=${PRIMO_PHASE_SHARDS:-8}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
MANIFESTS=${PRIMO_PHASE_MANIFESTS:-"benchmarks/qfuf-smac-mixed-train.txt benchmarks/qfuf-smac-mixed-validate.txt benchmarks/qfuf-smac-equality-guard.txt"}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

if (( TASK_ID >= SHARDS * 2 )); then
  echo "task $TASK_ID is outside the active 2 x $SHARDS shard range; exiting"
  exit 0
fi

variant_index=$((TASK_ID / SHARDS))
shard_index=$((TASK_ID % SHARDS))
case "$variant_index" in
  0)
    variant=default
    extra_args=()
    ;;
  1)
    variant=auto
    extra_args=(--theory-phase=auto)
    ;;
  *)
    echo "unsupported variant index: $variant_index" >&2
    exit 2
    ;;
esac

manifest="$OUT_ROOT/manifest-${TASK_ID}.txt"
manifest_tmp="$manifest.$$"
{
  for input_manifest in $MANIFESTS; do
    sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d; s#^tmp/QF_UF/##' "$input_manifest"
  done
} | awk '!seen[$0]++' > "$manifest_tmp"
mv "$manifest_tmp" "$manifest"

"$SOLVER" --version
python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --solver "$SOLVER" \
  --solver-arg=--quiet \
  --solver-arg=--stats \
  "${extra_args[@]/#/--solver-arg=}" \
  --timeout "$TIMEOUT" \
  --manifest "$manifest" \
  --shards "$SHARDS" \
  --shard-index "$shard_index" \
  --out "$OUT_ROOT/$variant/shard-$shard_index.csv" \
  --summary "$OUT_ROOT/$variant/shard-$shard_index-summary.csv" \
  --log-dir "$OUT_ROOT/$variant/logs/$shard_index"
