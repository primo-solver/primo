#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuf-boolean-fuzz-gate
#SBATCH --array=0-8
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Differential gate for `Bool` used in every capacity a QF_UF problem allows:
# as a function argument, as a function result, as a compound formula in
# argument position, nested, travelling through an uninterpreted sort, and in
# inferences that need `Bool` to have exactly two elements.
#
# Why this gate exists separately from slurm_qfuf_fuzz_gate.sbatch: FuzzSMT
# does not generate uninterpreted functions over `Bool`. No file in the
# SMT-LIB QF_UF division uses the shape either (recorded in
# benchmarks/seeds/qf_uf/seed_bool_argument_predicate.smt2), so neither
# mutation fuzzing nor corpus runs can reach it. The shape has now produced
# **two** wrong answers -- docs/bool-argument-congruence-bug.md (2026-08-07,
# forward congruence, fixed) and bugs/qf-uf-boolean-argument-congruence
# (2026-08-26, the converse, open) -- and both were found by hand rather than
# by a gate.
#
# One shard per shape, so a failure names the capacity that broke rather than
# a case number. Array size must stay in step with the shape list below.
#
# bugs/qf-uf-boolean-argument-congruence is fixed (fa44295 + the pure-polarity
# follow-up), so this gate is expected to be CLEAN and any failure is a
# regression. It is also worth knowing what a failure looks like when the
# reference is simply absent: every case comes back REFERENCE_UNUSABLE, which
# the classifier deliberately treats as a failure rather than a pass. Point
# PRIMO_FUZZ_REF at an installed z3 if the workspace does not build one.

set -uo pipefail

WORKDIR=${PRIMO_WORKDIR:-${SLURM_SUBMIT_DIR:-$PWD}}
SOLVER=${PRIMO_FUZZ_SOLVER:-build-slurm-rel/primo}
REF=${PRIMO_FUZZ_REF:-contrib/local/bin/z3}
COUNT=${PRIMO_FUZZ_COUNT:-1000}
TIMEOUT=${PRIMO_FUZZ_TIMEOUT:-10}
SEED_BASE=${PRIMO_FUZZ_SEED_BASE:-0}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/qf-uf-boolean-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

SHAPES=(argument result compound nested mixed-sort two-valued converse pure-polarity all)

cd "$WORKDIR" || exit 2
mkdir -p logs "$OUT_ROOT"

if (( TASK_ID >= ${#SHAPES[@]} )); then
  echo "task $TASK_ID is outside the ${#SHAPES[@]}-shape range; exiting"
  exit 0
fi

shape=${SHAPES[$TASK_ID]}
echo "task $TASK_ID: shape=$shape count=$COUNT seed=$((SEED_BASE + TASK_ID))"
"$SOLVER" --version || true

python3 scripts/qf_uf_boolean_fuzz.py \
  --solver "$SOLVER" \
  --ref "$REF" \
  --count "$COUNT" \
  --seed "$((SEED_BASE + TASK_ID))" \
  --timeout "$TIMEOUT" \
  --shape "$shape" \
  --out "$OUT_ROOT/$shape"
status=$?

# Report the per-shape verdict in the log even when the script exits nonzero,
# so a sweep over shards can be read without opening every summary.csv.
echo "shape=$shape exit=$status"
if [ -f "$OUT_ROOT/$shape/summary.csv" ]; then
  python3 - "$OUT_ROOT/$shape/summary.csv" <<'PY'
import collections, csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
counts = collections.Counter(row["outcome"] for row in rows)
print("cases=%d %s" % (len(rows), dict(counts)))
PY
else
  echo "NO SUMMARY WRITTEN -- shard died before finishing; do not read this gate as clean"
fi
exit $status
