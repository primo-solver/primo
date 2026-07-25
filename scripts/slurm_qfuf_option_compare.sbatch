#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuf-options
#SBATCH --array=0-47
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=01:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
SOLVER=${PRIMO_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
OUT_ROOT=${PRIMO_OPTION_OUT_ROOT:-"$WORKDIR/results/qfuf-option-compare-${SLURM_ARRAY_JOB_ID:-manual}"}
TIMEOUT=${PRIMO_OPTION_TIMEOUT:-60}
SHARDS=${PRIMO_OPTION_SHARDS:-8}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
MANIFESTS=${PRIMO_OPTION_MANIFESTS:-"benchmarks/qfuf-smac-mixed-train.txt benchmarks/qfuf-smac-mixed-validate.txt benchmarks/qfuf-smac-equality-guard.txt"}
VARIANTS=${PRIMO_OPTION_VARIANTS:-"default= basic=--qf-uf-preprocess=basic basic-polarity=--qf-uf-preprocess=basic,--theory-phase=polarity"}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

IFS=' ' read -r -a variant_specs <<< "$VARIANTS"
variant_count=${#variant_specs[@]}
if (( variant_count == 0 )); then
  echo "PRIMO_OPTION_VARIANTS did not define any variants" >&2
  exit 2
fi

if (( TASK_ID >= SHARDS * variant_count )); then
  echo "task $TASK_ID is outside the active $variant_count x $SHARDS shard range; exiting"
  exit 0
fi

variant_index=$((TASK_ID / SHARDS))
shard_index=$((TASK_ID % SHARDS))
variant_spec=${variant_specs[$variant_index]}
variant=${variant_spec%%=*}
arg_csv=${variant_spec#*=}
if [[ -z "$variant" || "$variant" == "$variant_spec" ]]; then
  echo "invalid variant spec '$variant_spec'; expected name=arg1,arg2" >&2
  exit 2
fi

extra_args=()
if [[ -n "$arg_csv" ]]; then
  IFS=',' read -r -a extra_args <<< "$arg_csv"
fi

manifest="$OUT_ROOT/manifest-${TASK_ID}.txt"
manifest_tmp="$manifest.$$"
{
  for input_manifest in $MANIFESTS; do
    sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d; s#^tmp/QF_UF/##' "$input_manifest"
  done
} | awk '!seen[$0]++' > "$manifest_tmp"
mv "$manifest_tmp" "$manifest"

"$SOLVER" --version
printf 'variant=%s args=%s\n' "$variant" "${extra_args[*]:-}"
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
