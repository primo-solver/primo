#!/usr/bin/env bash
# Pool-based checks of Primo's quantified UF answers with cvc5
# (scripts/quantifier_pool_check.py). Task i checks the files at positions
# congruent to i modulo PRIMO_POOL_SHARDS in PRIMO_POOL_LIST, one JSON line per
# file in $PRIMO_POOL_OUT/results-<task>.jsonl. Keep --array in step with the
# shard count by hand.
#SBATCH --job-name=primo-quant-pool-check
#SBATCH --array=0-39
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=04:00:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail

cd "${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the prepared workspace}"
LIST=${PRIMO_POOL_LIST:?PRIMO_POOL_LIST must name a file with one input path per line}
OUT=${PRIMO_POOL_OUT:?PRIMO_POOL_OUT must name the output directory}
SHARDS=${PRIMO_POOL_SHARDS:-40}
TIMEOUT=${PRIMO_POOL_TIMEOUT:-60}
# Extra script arguments, split on spaces, for example
# "--force --primo-arg=--quantifier-max-instances=5000".
read -r -a EXTRA <<< "${PRIMO_POOL_ARGS:-}"
TASK=${SLURM_ARRAY_TASK_ID:-0}

mkdir -p "$OUT" slurm_logs
build-slurm-rel/primo --version > "$OUT/primo-version-$TASK.txt"
results="$OUT/results-$TASK.jsonl"
: > "$results"
awk -v shards="$SHARDS" -v task="$TASK" '(NR - 1) % shards == task' "$LIST" |
  while IFS= read -r file; do
    work=$(mktemp -d)
    status=0
    python3 scripts/quantifier_pool_check.py "$file" --primo build-slurm-rel/primo \
      --cvc5 contrib/local/bin/cvc5 --primo-timeout "$TIMEOUT" --cvc5-timeout "$TIMEOUT" \
      --work-dir "$work" "${EXTRA[@]}" >> "$results" || status=$?
    # 3 is a refuted answer or an unsat control, already reported on its line.
    if [[ $status -ne 0 && $status -ne 3 ]]; then
      printf '{"file": "%s", "verdict": "script-error", "status": %d}\n' "$file" "$status" >> "$results"
    fi
    rm -rf "$work"
  done
