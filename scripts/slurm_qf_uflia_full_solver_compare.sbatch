#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuflia-full
#SBATCH --array=0-31
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Full-corpus QF_UFLIA comparison. TASK_ID decomposes into a solver index and
# a deterministic shard index, so every solver sees the same 659 SMT-LIB
# instances. Keep --array synchronized with four solvers times eight shards.

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-${SLURM_SUBMIT_DIR:-$PWD}}
ROOT=${PRIMO_COMPARE_ROOT:-tmp/QF_UFLIA}
TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-60}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/qf-uflia-full-${SLURM_ARRAY_JOB_ID:-manual}"}
SOLVERS=${PRIMO_COMPARE_SOLVERS:-"primo=build-slurm-rel/primo --quiet --stats;z3=contrib/local/bin/z3;cvc5=contrib/local/bin/cvc5 --lang smt2;opensmt=contrib/local/bin/opensmt"}
SHARDS_PER_SOLVER=${PRIMO_COMPARE_SHARDS:-8}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

if [[ ! -d "$ROOT" ]]; then
  echo "QF_UFLIA root '$ROOT' not found; stage sandbox/QF_UFLIA.tar.zst first." >&2
  exit 2
fi

IFS=';' read -r -a solver_specs <<< "$SOLVERS"
num_solvers=${#solver_specs[@]}
total_tasks=$((SHARDS_PER_SOLVER * num_solvers))
if ((TASK_ID >= total_tasks)); then
  echo "task $TASK_ID is outside the $total_tasks-task range" >&2
  exit 2
fi

solver_index=$((TASK_ID / SHARDS_PER_SOLVER))
shard_index=$((TASK_ID % SHARDS_PER_SOLVER))
spec=${solver_specs[$solver_index]}
name=${spec%%=*}
command_str=${spec#*=}
if [[ -z "$name" || "$name" == "$spec" ]]; then
  echo "invalid solver spec '$spec'; expected name=command" >&2
  exit 2
fi

IFS=' ' read -r -a solver_words <<< "$command_str"
solver_binary=${solver_words[0]}
solver_extra_args=("${solver_words[@]:1}")

echo "task=$TASK_ID solver=$name shard=$shard_index/$SHARDS_PER_SOLVER"
echo "command=${solver_words[*]}"
"$solver_binary" --version || true

python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --root "$ROOT" \
  --solver "$solver_binary" \
  "${solver_extra_args[@]/#/--solver-arg=}" \
  --timeout "$TIMEOUT" \
  --shards "$SHARDS_PER_SOLVER" \
  --shard-index "$shard_index" \
  --out "$OUT_ROOT/$name/shard-$shard_index.csv" \
  --summary "$OUT_ROOT/$name/shard-$shard_index-summary.csv" \
  --log-dir "$OUT_ROOT/$name/logs/shard-$shard_index"
