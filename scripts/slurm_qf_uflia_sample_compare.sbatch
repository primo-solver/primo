#!/usr/bin/env bash
# Compare Primo's QF_UFLIA routes with the standard reference solvers on the
# fixed representative archive sample. One array task owns one solver arm;
# bench_qf_uf_full.py journals every completed row before proceeding.
#SBATCH --job-name=primo-qfuflia-sample
#SBATCH --array=0-7
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=slurm_logs/%x-%A-%a.out
#SBATCH --error=slurm_logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
ROOT=${PRIMO_COMPARE_ROOT:-tmp/QF_UFLIA}
MANIFEST=${PRIMO_COMPARE_MANIFEST:-benchmarks/qf_uflia-smtlib-representative-20.txt}
TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-30}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/qf-uflia-sample-${SLURM_ARRAY_JOB_ID:-manual}"}
SOLVERS=${PRIMO_COMPARE_SOLVERS:-"primo-default=build-slurm-rel/primo --quiet --stats;primo-complete=build-slurm-rel/primo --quiet --stats --no-qf-uflia-model-combination --no-qf-uflia-online-components;primo-model-only=build-slurm-rel/primo --quiet --stats --no-qf-uflia-online-components;primo-online-complete=build-slurm-rel/primo --quiet --stats --no-qf-uflia-model-combination;primo-sparse-online=build-slurm-rel/primo --quiet --stats --qf-uflia-model-decision-budget=512;z3=contrib/local/bin/z3 -st;cvc5=contrib/local/bin/cvc5 --lang smt2 --stats;opensmt=contrib/local/bin/opensmt"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"

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
