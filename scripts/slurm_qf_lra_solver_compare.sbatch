#!/usr/bin/env bash
#SBATCH --job-name=primo-qflra-solver-compare
#SBATCH --array=0-2
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Competitiveness-roadmap item 0: empirical Primo-vs-reference-solver
# baseline over a QF_LRA sample. One array task per solver (default:
# primo, z3, cvc5) -- each runs the full manifest sequentially within its
# own task, since bench_qf_uf_full.py's own --shards/--shard-index would
# split *within* a solver, not across solvers. Reusable for future
# QF_LRA (or, with a different --root/manifest, QF_UF) solver
# comparisons, not just this one-off run.
#
# PRIMO_COMPARE_SOLVERS: space-separated "name=command" pairs, command is
# everything after the first "=" (may contain its own spaces/flags, e.g.
# "primo=build-slurm-rel/primo --quiet").

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-/home/janotmik/primo-cadical-profile}
ROOT=${PRIMO_COMPARE_ROOT:-tmp/QF_LRA}
MANIFEST=${PRIMO_COMPARE_MANIFEST:-benchmarks/qf-lra-stratified-sample.txt}
TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-10}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/qf-lra-solver-compare-${SLURM_ARRAY_JOB_ID:-manual}"}
SOLVERS=${PRIMO_COMPARE_SOLVERS:-"primo=build-slurm-rel/primo --quiet z3=contrib/local/bin/z3 cvc5=contrib/local/bin/cvc5"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

IFS=' ' read -r -a solver_specs <<< "$SOLVERS"
if (( TASK_ID >= ${#solver_specs[@]} )); then
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

echo "task $TASK_ID: solver=$name command=${solver_words[*]}"
"${solver_words[0]}" --version || true

python3 scripts/bench_qf_uf_full.py \
  --repo "$WORKDIR" \
  --root "$ROOT" \
  --manifest "$MANIFEST" \
  --solver "${solver_words[@]}" \
  --timeout "$TIMEOUT" \
  --out "$OUT_ROOT/$name.csv" \
  --summary "$OUT_ROOT/$name-summary.csv"
