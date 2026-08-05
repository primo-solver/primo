#!/usr/bin/env bash
# SLURM gate for QF_LRA answer fuzzing plus model validation.
#
# Submit with a fresh PRIMO_WORKDIR. The default layout is six FuzzSMT shards
# of 500 cases each, followed by one opfuzz task and two YinYang/typefuzz tasks
# over real QF_LRA seeds. FuzzSMT shards validate produced models after answer
# comparison; mutation tasks remain answer-differential fuzzing.

#SBATCH --job-name=primo-qflra-model-fuzz
#SBATCH --array=0-8
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the fresh remote workspace}
SOLVER_BIN=${PRIMO_FUZZ_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
# Extra solver flags to gate, e.g. an opt-in preprocessing pass. Kept separate
# from PRIMO_FUZZ_SOLVER because the Dolmen model validator takes the solver as
# a bare executable plus discrete --solver-arg entries, so flags folded into
# SOLVER_BIN would reach the answer fuzzer but silently NOT the model check --
# which is the half that matters for a pass that reconstructs models.
SOLVER_EXTRA_ARGS=${PRIMO_FUZZ_SOLVER_EXTRA_ARGS:-""}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/qf-lra-model-gate-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

FUZZSMT_SHARDS=${PRIMO_FUZZ_FUZZSMT_SHARDS:-6}
FUZZSMT_COUNT=${PRIMO_FUZZ_FUZZSMT_COUNT:-500}
FUZZSMT_TIMEOUT=${PRIMO_FUZZ_FUZZSMT_TIMEOUT:-5}
FUZZSMT_SEED_BASE=${PRIMO_FUZZ_FUZZSMT_SEED_BASE:-$(( ${SLURM_ARRAY_JOB_ID:-500000} % 900000 + 100000 ))}
FUZZSMT_ARGS=${PRIMO_FUZZ_FUZZSMT_ARGS:-"-ms 1 -Ms 3 -mv 1 -Mv 3 -mf 1 -mp 1 -ma 1 -Ma 2"}
MODEL_TIMEOUT=${PRIMO_FUZZ_MODEL_TIMEOUT:-10}
MODEL_DOLMEN_TIMEOUT=${PRIMO_FUZZ_MODEL_DOLMEN_TIMEOUT:-10}

OPFUZZ_TASKS=${PRIMO_FUZZ_OPFUZZ_TASKS:-1}
OPFUZZ_SEEDS=${PRIMO_FUZZ_OPFUZZ_SEEDS:-"$WORKDIR/benchmarks/seeds/qf_lra_real"}
OPFUZZ_ITERATIONS=${PRIMO_FUZZ_OPFUZZ_ITERATIONS:-200}
OPFUZZ_TIMEOUT=${PRIMO_FUZZ_OPFUZZ_TIMEOUT:-5}

YINYANG_SHARDS=${PRIMO_FUZZ_YINYANG_SHARDS:-2}
YINYANG_SEEDS=${PRIMO_FUZZ_YINYANG_SEEDS:-"$WORKDIR/benchmarks/seeds/qf_lra_real"}
YINYANG_ITERATIONS=${PRIMO_FUZZ_YINYANG_ITERATIONS:-100}
YINYANG_TIMEOUT=${PRIMO_FUZZ_YINYANG_TIMEOUT:-5}
YINYANG_WALL_SECONDS=${PRIMO_FUZZ_YINYANG_WALL_SECONDS:-1500}

MUTATION_REF=${PRIMO_FUZZ_MUTATION_REF:-"z3"}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

PATH="$WORKDIR/contrib/local/bin:$PATH"
if [ -d "$WORKDIR/.venv-yinyang/bin" ]; then
  PATH="$WORKDIR/.venv-yinyang/bin:$PATH"
fi
export PATH

for required in fuzzsmt z3; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required tool '$required' not found on PATH" >&2
    exit 2
  fi
done

opfuzz_start=$FUZZSMT_SHARDS
yinyang_start=$((opfuzz_start + OPFUZZ_TASKS))
total_tasks=$((yinyang_start + YINYANG_SHARDS))

if (( TASK_ID >= total_tasks )); then
  echo "task $TASK_ID is outside the active $total_tasks-task range"
  exit 0
fi

IFS=' ' read -r -a solver_words <<< "$SOLVER_BIN"
IFS=' ' read -r -a extra_args <<< "$SOLVER_EXTRA_ARGS"
model_solver_args=(--solver-arg=--quiet)
for extra in ${extra_args+"${extra_args[@]}"}; do
  model_solver_args+=("--solver-arg=$extra")
done
"${solver_words[0]}" --version

if (( TASK_ID < opfuzz_start )); then
  shard=$TASK_ID
  seed=$((FUZZSMT_SEED_BASE + shard))
  out="$OUT_ROOT/fuzzsmt/shard-$shard"
  model_out="$OUT_ROOT/model-validation/shard-$shard"
  mkdir -p "$out" "$model_out"
  echo "task $TASK_ID: QF_LRA FuzzSMT shard $shard seed $seed"
  python3 scripts/fuzz_external.py \
    --fuzzer fuzzsmt \
    --solver "$SOLVER_BIN --quiet $SOLVER_EXTRA_ARGS" \
    --refs z3 \
    --logic QF_LRA \
    --count "$FUZZSMT_COUNT" \
    --timeout "$FUZZSMT_TIMEOUT" \
    --seed "$seed" \
    --fuzzer-args "$FUZZSMT_ARGS" \
    --out "$out"
  python3 scripts/validate_model.py \
    --solver "${solver_words[0]}" \
    "${model_solver_args[@]}" \
    --timeout "$MODEL_TIMEOUT" \
    --dolmen-timeout "$MODEL_DOLMEN_TIMEOUT" \
    --out "$model_out" \
    "$out"/generated/*.smt2
elif (( TASK_ID < yinyang_start )); then
  out="$OUT_ROOT/opfuzz/task-$TASK_ID"
  mkdir -p "$out"
  echo "task $TASK_ID: opfuzz over $OPFUZZ_SEEDS"
  scripts/yinyang_fuzz.sh \
    --typefuzz opfuzz \
    --seeds "$OPFUZZ_SEEDS" \
    --solver "$SOLVER_BIN --quiet $SOLVER_EXTRA_ARGS" \
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
    --solver "$SOLVER_BIN --quiet $SOLVER_EXTRA_ARGS" \
    --ref "$MUTATION_REF" \
    --seconds "$YINYANG_WALL_SECONDS" \
    --iterations "$YINYANG_ITERATIONS" \
    --solver-timeout "$YINYANG_TIMEOUT" \
    --bugs "$out/bugs" \
    --scratch "$out/scratch"
fi
