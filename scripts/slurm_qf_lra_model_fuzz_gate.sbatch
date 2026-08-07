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
OPFUZZ_SEEDS=${PRIMO_FUZZ_OPFUZZ_SEEDS:-"$WORKDIR/benchmarks/seeds/qf_lra_division"}
OPFUZZ_ITERATIONS=${PRIMO_FUZZ_OPFUZZ_ITERATIONS:-200}
OPFUZZ_TIMEOUT=${PRIMO_FUZZ_OPFUZZ_TIMEOUT:-5}

YINYANG_SHARDS=${PRIMO_FUZZ_YINYANG_SHARDS:-2}
YINYANG_SEEDS=${PRIMO_FUZZ_YINYANG_SEEDS:-"$WORKDIR/benchmarks/seeds/qf_lra_division"}
YINYANG_ITERATIONS=${PRIMO_FUZZ_YINYANG_ITERATIONS:-100}
YINYANG_TIMEOUT=${PRIMO_FUZZ_YINYANG_TIMEOUT:-5}
YINYANG_WALL_SECONDS=${PRIMO_FUZZ_YINYANG_WALL_SECONDS:-1500}

MUTATION_REF=${PRIMO_FUZZ_MUTATION_REF:-"z3 model_validate=true;$WORKDIR/scripts/yices_smt2_ref.sh"}

# The 2026-08-02 QF_LRA mutation hardening, which slurm_qfuf_fuzz_gate.sbatch
# received and this gate did not. Each piece is load-bearing:
#
# * FILE_SIZE_LIMIT -- typefuzz's own default is 20000 bytes and it drops
#   oversized seeds without saying so beyond a "N valid, M invalid" count.
#   That silently truncated the family-complete corpus back to 11 of its 17
#   families (job 118543), discarding latendresse and LassoRanker, the two
#   families it exists to cover. 200000 matches the --max-bytes that
#   scripts/build_fuzz_seed_corpus.py selects under, so the corpus builder
#   decides the corpus rather than the limit. The largest division seed is
#   179,604 bytes -- a 12% margin, so check "34 valid, 0 invalid" in the log.
# * The configs restrict mutation to operators that stay inside QF_LRA;
#   without them opfuzz produces (* x y) from (+ x y), which is no longer
#   QF_LRA and burns the budget on mutants Primo rightly refuses.
# * --keep-logic makes every solver judge the mutant against the seed's logic
#   (and is what lets yices-smt2 read a mutant at all); --hoist-declarations
#   repairs typefuzz emitting a declaration after its first use. opfuzz does
#   not need the latter.
MUTATION_FILE_SIZE_LIMIT=${PRIMO_FUZZ_MUTATION_FILE_SIZE_LIMIT:-200000}
OPFUZZ_CONFIG=${PRIMO_FUZZ_OPFUZZ_CONFIG:-"$WORKDIR/benchmarks/fuzz-configs/qf_lra_operator_mutations.txt"}
YINYANG_CONFIG=${PRIMO_FUZZ_YINYANG_CONFIG:-"$WORKDIR/benchmarks/fuzz-configs/qf_lra_typefuzz.txt"}
MUTATION_KEEP_LOGIC=${PRIMO_FUZZ_MUTATION_KEEP_LOGIC-1}
MUTATION_HOIST_DECLARATIONS=${PRIMO_FUZZ_MUTATION_HOIST_DECLARATIONS-1}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

PATH="$WORKDIR/contrib/local/bin:$PATH"
if [ -d "$WORKDIR/.venv-yinyang/bin" ]; then
  PATH="$WORKDIR/.venv-yinyang/bin:$PATH"
fi
export PATH

# Cover every configured mutation reference, not just z3. MUTATION_REF now
# carries a second reference (the yices wrapper), and a missing second
# reference silently narrows the gate rather than failing it -- which is the
# whole failure family this check exists for. Mirrors the same loop in
# scripts/slurm_qfuf_fuzz_gate.sbatch.
required_tools=(fuzzsmt z3)
for ref in ${MUTATION_REF//;/ }; do
  case $ref in
    *=*) continue ;;  # solver options such as model_validate=true
  esac
  required_tools+=("$ref")
done
for required in "${required_tools[@]}"; do
  if ! command -v "$required" >/dev/null 2>&1 && [ ! -x "$required" ]; then
    echo "required tool '$required' not found on PATH; refusing to report a gate that ran nothing" >&2
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
    --file-size-limit "$MUTATION_FILE_SIZE_LIMIT" \
    ${OPFUZZ_CONFIG:+--config "$OPFUZZ_CONFIG"} \
    ${MUTATION_KEEP_LOGIC:+--keep-logic} \
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
    --file-size-limit "$MUTATION_FILE_SIZE_LIMIT" \
    ${YINYANG_CONFIG:+--config "$YINYANG_CONFIG"} \
    ${MUTATION_KEEP_LOGIC:+--keep-logic} \
    ${MUTATION_HOIST_DECLARATIONS:+--hoist-declarations} \
    --bugs "$out/bugs" \
    --scratch "$out/scratch"
fi
