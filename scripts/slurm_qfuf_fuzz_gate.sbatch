#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuf-fuzz-gate
#SBATCH --array=0-8
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Wall limit: a FuzzSMT shard is FUZZSMT_COUNT cases, each costing up to
# FUZZSMT_TIMEOUT for our solver plus the same again for the reference, so the
# worst case is roughly COUNT * 2 * TIMEOUT -- 500 * 2 * 5s = 83 minutes at
# the defaults. The previous 45-minute limit was under that by construction,
# and a shard that hits the wall is killed *before* fuzz_external.py writes
# its summary.csv, so the run silently reports fewer cases rather than
# failing: jobs 117248-117251 each lost exactly one shard that way (2500 of
# 3000 cases) and looked clean. If you raise FUZZSMT_COUNT or
# FUZZSMT_TIMEOUT, raise this to match, and always check that the number of
# summary.csv files equals the shard count before believing a gate result.
#
# Combined QF_UF fuzz gate: FuzzSMT (generator) + opfuzz + YinYang typefuzz
# (mutation), one array job. This is the campaign previously reconstructed
# by hand each time from docs/run-log.json notes (e.g. 2026-07-18 entries
# qfuf-fuzzsmt-yinyang-5de60d9 / -6f3cdca); it is now a checked-in script so
# it does not need to be re-derived.
#
# Task layout (defaults: 6 FuzzSMT shards + 1 opfuzz task + 2 YinYang shards
# = 9 tasks, array 0-8):
#   [0 .. FUZZSMT_SHARDS-1]                         FuzzSMT via fuzz_external.py
#   [FUZZSMT_SHARDS .. +OPFUZZ_TASKS-1]              opfuzz via yinyang_fuzz.sh --typefuzz opfuzz
#   [.. +YINYANG_SHARDS-1]                           YinYang typefuzz via yinyang_fuzz.sh
#
# If you override the *_SHARDS/*_TASKS env vars, also pass a matching
# --array=0-N to sbatch (SBATCH directives are static and cannot read env).
#
# Validate before submitting: python3 scripts/slurm_validate_sbatch.py \
#   scripts/slurm_qfuf_fuzz_gate.sbatch
#
# Submit: sbatch scripts/slurm_qfuf_fuzz_gate.sbatch
# Rerun with a fresh FuzzSMT corpus and same layout: just submit again
# (the seed base is derived from SLURM_ARRAY_JOB_ID by default).

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-/home/janotmik/primo-cadical-profile}
SOLVER_BIN=${PRIMO_FUZZ_SOLVER:-"$WORKDIR/build-slurm-fuzz/primo"}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/qfuf-gate-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

FUZZSMT_SHARDS=${PRIMO_FUZZ_FUZZSMT_SHARDS:-6}
FUZZSMT_COUNT=${PRIMO_FUZZ_FUZZSMT_COUNT:-500}
FUZZSMT_TIMEOUT=${PRIMO_FUZZ_FUZZSMT_TIMEOUT:-5}
FUZZSMT_LOGIC=${PRIMO_FUZZ_FUZZSMT_LOGIC:-QF_UF}
FUZZSMT_ARGS=${PRIMO_FUZZ_FUZZSMT_ARGS:-"-ms 1 -Ms 2 -mv 1 -Mv 2 -mf 1 -mp 1 -ma 1 -Ma 2"}
FUZZSMT_REF=${PRIMO_FUZZ_FUZZSMT_REF:-z3}
FUZZSMT_SEED_BASE=${PRIMO_FUZZ_FUZZSMT_SEED_BASE:-$(( ${SLURM_ARRAY_JOB_ID:-500000} % 900000 + 100000 ))}

OPFUZZ_TASKS=${PRIMO_FUZZ_OPFUZZ_TASKS:-1}
OPFUZZ_SEEDS=${PRIMO_FUZZ_OPFUZZ_SEEDS:-"$WORKDIR/benchmarks/seeds/qf_uf"}
OPFUZZ_ITERATIONS=${PRIMO_FUZZ_OPFUZZ_ITERATIONS:-200}
OPFUZZ_TIMEOUT=${PRIMO_FUZZ_OPFUZZ_TIMEOUT:-5}

YINYANG_SHARDS=${PRIMO_FUZZ_YINYANG_SHARDS:-2}
YINYANG_SEEDS=${PRIMO_FUZZ_YINYANG_SEEDS:-"$WORKDIR/benchmarks/seeds/qf_uf"}
YINYANG_ITERATIONS=${PRIMO_FUZZ_YINYANG_ITERATIONS:-100}
YINYANG_TIMEOUT=${PRIMO_FUZZ_YINYANG_TIMEOUT:-5}
YINYANG_WALL_SECONDS=${PRIMO_FUZZ_YINYANG_WALL_SECONDS:-1500}

MUTATION_REF=${PRIMO_FUZZ_MUTATION_REF:-"z3 model_validate=true"}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

opfuzz_start=$FUZZSMT_SHARDS
yinyang_start=$((opfuzz_start + OPFUZZ_TASKS))
total_tasks=$((yinyang_start + YINYANG_SHARDS))

if (( TASK_ID >= total_tasks )); then
  echo "task $TASK_ID is outside the active $total_tasks-task range; exiting"
  exit 0
fi

"$SOLVER_BIN" --version || true

if (( TASK_ID < opfuzz_start )); then
  shard=$TASK_ID
  seed=$((FUZZSMT_SEED_BASE + shard))
  out="$OUT_ROOT/fuzzsmt/shard-$shard"
  mkdir -p "$out"
  echo "task $TASK_ID: fuzzsmt shard $shard seed $seed"
  python3 scripts/fuzz_external.py \
    --fuzzer fuzzsmt \
    --solver "$SOLVER_BIN --quiet" \
    --refs $FUZZSMT_REF \
    --logic "$FUZZSMT_LOGIC" \
    --count "$FUZZSMT_COUNT" \
    --timeout "$FUZZSMT_TIMEOUT" \
    --seed "$seed" \
    --fuzzer-args "$FUZZSMT_ARGS" \
    --out "$out"
elif (( TASK_ID < yinyang_start )); then
  out="$OUT_ROOT/opfuzz/task-$TASK_ID"
  mkdir -p "$out"
  echo "task $TASK_ID: opfuzz over $OPFUZZ_SEEDS"
  scripts/yinyang_fuzz.sh \
    --typefuzz opfuzz \
    --seeds "$OPFUZZ_SEEDS" \
    --solver "$SOLVER_BIN --quiet" \
    --ref "$MUTATION_REF" \
    --seconds "$YINYANG_WALL_SECONDS" \
    --iterations "$OPFUZZ_ITERATIONS" \
    --solver-timeout "$OPFUZZ_TIMEOUT" \
    --bugs "$out/bugs" \
    --scratch "$out/scratch"
else
  out="$OUT_ROOT/yinyang/task-$TASK_ID"
  mkdir -p "$out"
  echo "task $TASK_ID: yinyang typefuzz over $YINYANG_SEEDS"
  scripts/yinyang_fuzz.sh \
    --seeds "$YINYANG_SEEDS" \
    --solver "$SOLVER_BIN --quiet" \
    --ref "$MUTATION_REF" \
    --seconds "$YINYANG_WALL_SECONDS" \
    --iterations "$YINYANG_ITERATIONS" \
    --solver-timeout "$YINYANG_TIMEOUT" \
    --bugs "$out/bugs" \
    --scratch "$out/scratch"
fi
