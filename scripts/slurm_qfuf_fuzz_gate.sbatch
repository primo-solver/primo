#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuf-fuzz-gate
#SBATCH --array=0-8
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Wall limit. Two separate things to know here.
#
# 1. A shard that hits the wall is killed *before* fuzz_external.py writes its
#    summary.csv, so the shard vanishes from the results instead of failing:
#    the gate reports fewer cases and otherwise looks perfectly clean. Jobs
#    117248-117251 each lost exactly one shard that way (2500 of 3000 cases)
#    and read as 0 crashes / 0 wrong answers. Always check that the number of
#    summary.csv files equals the shard count before believing a gate result;
#    scripts/summarize_fuzz_results.py prints it for this reason.
#
# 2. The bound itself: a shard is FUZZSMT_COUNT cases, each costing up to
#    FUZZSMT_TIMEOUT for our solver plus the same for the reference, so the
#    worst case is roughly COUNT * 2 * TIMEOUT -- 500 * 2 * 5s = 83 minutes at
#    the defaults, which the previous 45-minute limit was under. Raise this if
#    you raise either.
#
# That arithmetic was NOT what caused the 117248-117251 losses, though, and
# the investigation is worth not repeating: all four lost shards were the same
# FuzzSMT seed (217251) across four different solver configurations, while the
# one job whose seeds skipped it completed every shard. That looks exactly like
# a solver hang on one input, and is not: the same seed completes 500/500
# locally in ~40s, on a compute node writing to this same NFS tree in 126s, and
# again to node-local /tmp; generation of its 500 files takes 0.6s remotely;
# peak RSS is ~95MB against the 2G cgroup. The failures happened while the
# cluster had 460+ queued jobs from other users and did not reproduce once it
# drained. Treat a whole-shard timeout as an environment signal first, but
# check the seed pattern -- if one seed fails across configurations *and*
# reproduces on a quiet cluster, that would be the real thing.
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

# The provisioned tooling (fuzzsmt, z3, cvc5) lives here. Put it on PATH
# inside the job rather than relying on --export=ALL to carry it in: that
# inherits the *submitting* shell's PATH, so a gate submitted over
# `ssh host bash -s` silently found no fuzzer while the same job submitted
# from an interactive shell ran fine. Job 117585 reported COMPLETED with
# SKIPPED_MISSING_FUZZER as its only summary row -- a green gate that tested
# nothing.
PATH="$WORKDIR/contrib/local/bin:$PATH"
export PATH

# And fail loudly rather than skipping: fuzz_external.py treats a missing
# fuzzer as "skip this stage" and exits 0, which is the right behaviour for a
# local smoke run and exactly the wrong one for a gate whose entire purpose
# is to have run.
for required in fuzzsmt z3; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required tool '$required' not found on PATH; refusing to report a gate that ran nothing" >&2
    exit 2
  fi
done

opfuzz_start=$FUZZSMT_SHARDS
yinyang_start=$((opfuzz_start + OPFUZZ_TASKS))
total_tasks=$((yinyang_start + YINYANG_SHARDS))

if (( TASK_ID >= total_tasks )); then
  echo "task $TASK_ID is outside the active $total_tasks-task range; exiting"
  exit 0
fi

# PRIMO_FUZZ_SOLVER may carry flags (e.g. "path/primo --lra-fixed-elimination"),
# since every --solver use below interpolates it into a string that
# fuzz_external.py shlex-splits. The provenance banner has to take just the
# binary, or it degrades into a "command not found" that `|| true` hides --
# and that banner is the only record of which build a gate actually ran.
IFS=' ' read -r -a solver_words <<< "$SOLVER_BIN"
"${solver_words[0]}" --version || true

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
