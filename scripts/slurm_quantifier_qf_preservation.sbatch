#!/usr/bin/env bash
#SBATCH --job-name=primo-quantifier-qf-preserve
#SBATCH --array=0-15
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=02:30:00
#SBATCH --output=slurm_logs/%x-%A-%a.out
#SBATCH --error=slurm_logs/%x-%A-%a.err

# Quantifier-free preservation for the quantifier branch (docs/quantifiers.md):
# a baseline and a candidate binary run on the same files, and each file must
# give the same answer and the same non-timing statistics. The quantifier
# milestones change shared frontend, core, clausifier and driver code, and
# are meant to leave quantifier-free behaviour identical. Answers-and-counters
# equality is what is checked here; this is not a runtime comparison.
#
# Every task runs scripts/compare_local_solver_stats.py on the files whose
# position in the sorted corpus list is congruent to the task id modulo the
# shard count. Keep --array in step with PRIMO_PRESERVE_SHARDS by hand.

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-${SLURM_SUBMIT_DIR:-$PWD}}
BASELINE=${PRIMO_BASELINE:?PRIMO_BASELINE must name the baseline primo binary}
CANDIDATE=${PRIMO_CANDIDATE:?PRIMO_CANDIDATE must name the candidate primo binary}
ROOT=${PRIMO_PRESERVE_ROOT:?PRIMO_PRESERVE_ROOT must name the corpus root}
TIMEOUT=${PRIMO_PRESERVE_TIMEOUT:-60}
SHARDS=${PRIMO_PRESERVE_SHARDS:-16}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
OUT_ROOT=${PRIMO_PRESERVE_OUT_ROOT:-"$WORKDIR/results/quantifier-qf-preservation-${SLURM_ARRAY_JOB_ID:-manual}"}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"

if (( TASK_ID >= SHARDS )); then
  echo "task $TASK_ID is outside the $SHARDS-shard range; exiting"
  exit 0
fi
if [[ ! -d "$ROOT" ]]; then
  echo "corpus root does not exist: $ROOT" >&2
  exit 2
fi

"$BASELINE" --version | tee "$OUT_ROOT/baseline-version.txt"
"$CANDIDATE" --version | tee "$OUT_ROOT/candidate-version.txt"

manifest="$OUT_ROOT/shard-$TASK_ID.manifest"
(cd "$ROOT" && find . -name '*.smt2' -printf '%P\n' | LC_ALL=C sort) \
  | awk -v shards="$SHARDS" -v task="$TASK_ID" '(NR - 1) % shards == task' > "$manifest"
echo "task $TASK_ID: $(wc -l < "$manifest") files from $ROOT, timeout $TIMEOUT"

status=0
python3 scripts/compare_local_solver_stats.py \
  --baseline "$BASELINE" \
  --candidate "$CANDIDATE" \
  --root "$ROOT" \
  --manifest "$manifest" \
  --output "$OUT_ROOT/shard-$TASK_ID" \
  --repeats 1 \
  --timeout "$TIMEOUT" || status=$?
# Exit status 1 only means some file differed; the summary says which.
if (( status > 1 )); then
  exit "$status"
fi
