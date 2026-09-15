#!/usr/bin/env bash
#SBATCH --job-name=primo-uf-quantifier-compare
#SBATCH --array=0-239
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=03:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Full-corpus answer check of the quantified UF route (docs/quantifiers.md, M4)
# over the SMT-LIB UF division (7,590 files), against declared :status and
# against Z3 and cvc5. It uses the sharding scheme of
# slurm_qf_lia_full_solver_compare.sbatch: TASK_ID decomposes into (solver,
# shard), and bench_qf_uf_full.py partitions the sorted file list so that every
# solver's shard N holds the same files.
#
# PRIMO_COMPARE_SOLVERS: semicolon-separated "name=command" pairs. The array
# size must be PRIMO_COMPARE_SHARDS * (number of solvers) - 1, kept in sync
# with --array by hand. The UF corpus is not synced by
# slurm_prepare_benchmark_workspace.py; stage it and point
# PRIMO_COMPARE_ROOT at it.

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-${SLURM_SUBMIT_DIR:-$PWD}}
ROOT=${PRIMO_COMPARE_ROOT:-"$HOME/primo-quantifiers-data/non-incremental/UF"}
TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-60}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/uf-quantifier-compare-${SLURM_ARRAY_JOB_ID:-manual}"}
SOLVERS=${PRIMO_COMPARE_SOLVERS:-"primo=build-slurm-rel/primo --quiet;z3=contrib/local/bin/z3;cvc5=contrib/local/bin/cvc5"}
SHARDS_PER_SOLVER=${PRIMO_COMPARE_SHARDS:-80}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

if [ ! -d "$ROOT" ]; then
  echo "UF root '$ROOT' not found; stage the corpus first." >&2
  exit 2
fi

IFS=';' read -r -a solver_specs <<< "$SOLVERS"
num_solvers=${#solver_specs[@]}
total_tasks=$((SHARDS_PER_SOLVER * num_solvers))
if (( TASK_ID >= total_tasks )); then
  echo "task $TASK_ID is outside the $total_tasks-task range ($num_solvers solvers x $SHARDS_PER_SOLVER shards); exiting"
  exit 0
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

echo "task $TASK_ID: solver=$name shard=$shard_index/$SHARDS_PER_SOLVER command=${solver_words[*]} root=$ROOT timeout=$TIMEOUT"
"$solver_binary" --version || true

# --solver takes only the base binary; extra flags go through --solver-arg
# (see slurm_qf_lia_full_solver_compare.sbatch).
python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --root "$ROOT" \
  --solver "$solver_binary" \
  "${solver_extra_args[@]/#/--solver-arg=}" \
  --timeout "$TIMEOUT" \
  --shards "$SHARDS_PER_SOLVER" \
  --shard-index "$shard_index" \
  --out "$OUT_ROOT/$name/shard-$shard_index.csv" \
  --summary "$OUT_ROOT/$name/shard-$shard_index-summary.csv"
