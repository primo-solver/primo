#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuflra-sample
#SBATCH --array=0-4
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Local tuning sample for mixed QF_UFLRA. One array task runs one
# solver/configuration over the same deterministic manifest.

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
ROOT=${PRIMO_COMPARE_ROOT:-tmp/QF_UFLRA}
MANIFEST=${PRIMO_COMPARE_MANIFEST:-benchmarks/qf_uflra-smtlib-sample-5-per-family.txt}
TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-10}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/qf-uflra-sample-${SLURM_ARRAY_JOB_ID:-manual}"}
SOLVERS=${PRIMO_COMPARE_SOLVERS:-"default=build-slurm-rel/primo --quiet --stats;first=build-slurm-rel/primo --quiet --stats --model-equality-branching;ranked=build-slurm-rel/primo --quiet --stats --model-equality-branching --model-equality-branch-policy ranked;adaptive=build-slurm-rel/primo --quiet --stats --model-equality-branching --model-equality-branch-policy adaptive;z3=contrib/local/bin/z3"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

IFS=';' read -r -a solver_specs <<< "$SOLVERS"
if ((TASK_ID >= ${#solver_specs[@]})); then
  echo "task $TASK_ID is outside the ${#solver_specs[@]}-solver range; exiting"
  exit 0
fi

spec=${solver_specs[$TASK_ID]}
name=${spec%%=*}
command_str=${spec#*=}
if [[ -z "$name" || "$name" == "$spec" ]]; then
  echo "invalid solver spec '$spec'; expected name=command" >&2
  exit 2
fi
IFS=' ' read -r -a solver_words <<< "$command_str"
solver_binary=${solver_words[0]}
solver_extra_args=("${solver_words[@]:1}")

echo "task $TASK_ID: solver=$name command=${solver_words[*]}"
"$solver_binary" --version || true

python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --root "$ROOT" \
  --manifest "$MANIFEST" \
  --solver "$solver_binary" \
  "${solver_extra_args[@]/#/--solver-arg=}" \
  --timeout "$TIMEOUT" \
  --out "$OUT_ROOT/$name.csv" \
  --summary "$OUT_ROOT/$name-summary.csv" \
  --log-dir "$OUT_ROOT/$name-logs"
