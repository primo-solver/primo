#!/usr/bin/env bash
# SLURM gate for QF_LIA answer fuzzing plus model validation.
#
# A sibling of slurm_qf_lra_model_fuzz_gate.sbatch, covering only that
# gate's FuzzSMT-shard portion: fuzzsmt generates fresh QF_LIA instances
# per array task (it supports the logic natively -- no fuzzer changes were
# needed), each shard is checked answer-differentially against z3, and
# every `sat` result is model-validated with Dolmen
# (scripts/validate_model.py).
#
# --dolmen-arg=--strict=false below is required, not cosmetic: FuzzSMT's
# QF_LIA generator routinely produces `(* <let-bound-literal>
# <compound-expression>)` terms, e.g. `(* e5 e11)` where e5 happens to be
# a let-bound `1`. That is still linear (a literal times a linear
# expression is linear) but is stricter than QF_LIA's SMT-LIB grammar,
# which only allows `(* <literal> <symbol>)`. Dolmen's default --strict=true
# treats that as a Fatal Warning and aborts the whole check-model run before
# it ever evaluates the model -- verified on the cluster 2026-08-19: 39/45
# sat cases from one real shard came back model-error (each falling
# through to validate_model.py's z3-Real-only fallback, which finds no
# assignments to check for an Int model and reports validation-unavailable)
# until this flag was added, after which all 45 passed via Dolmen directly.
# The z3 fallback stays Real-only/unfixed -- not needed once Dolmen itself
# accepts the input.
#
# Unlike the QF_LRA gate, there is no opfuzz/typefuzz mutation-fuzzing
# stage: that stage mutates a committed seed corpus
# (benchmarks/seeds/qf_lra_division), and no equivalent QF_LIA corpus
# exists yet. Adding one (and an opfuzz/typefuzz stage alongside it) is
# natural future work once real QF_LIA benchmarks are collected -- see
# docs/qf-lia-branch-and-bound.md's Fuzzing section.
#
# Submit with a fresh PRIMO_WORKDIR. Default layout: eight FuzzSMT shards
# of 500 cases each.

#SBATCH --job-name=primo-qflia-model-fuzz
#SBATCH --array=0-7
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the fresh remote workspace}
SOLVER_BIN=${PRIMO_FUZZ_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
# See slurm_qf_lra_model_fuzz_gate.sbatch's own comment on why this is kept
# separate from PRIMO_FUZZ_SOLVER: the Dolmen model validator takes the
# solver as a bare executable plus discrete --solver-arg entries, so flags
# folded into SOLVER_BIN would reach the answer fuzzer but not the model
# check.
SOLVER_EXTRA_ARGS=${PRIMO_FUZZ_SOLVER_EXTRA_ARGS:-""}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/qf-lia-model-gate-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

FUZZSMT_SHARDS=${PRIMO_FUZZ_FUZZSMT_SHARDS:-8}
FUZZSMT_COUNT=${PRIMO_FUZZ_FUZZSMT_COUNT:-500}
FUZZSMT_TIMEOUT=${PRIMO_FUZZ_FUZZSMT_TIMEOUT:-5}
FUZZSMT_SEED_BASE=${PRIMO_FUZZ_FUZZSMT_SEED_BASE:-$(( ${SLURM_ARRAY_JOB_ID:-500000} % 900000 + 100000 ))}
FUZZSMT_ARGS=${PRIMO_FUZZ_FUZZSMT_ARGS:-"-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4"}
MODEL_TIMEOUT=${PRIMO_FUZZ_MODEL_TIMEOUT:-10}
MODEL_DOLMEN_TIMEOUT=${PRIMO_FUZZ_MODEL_DOLMEN_TIMEOUT:-10}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

PATH="$WORKDIR/contrib/local/bin:$PATH"
export PATH

required_tools=(fuzzsmt z3 dolmen)
for required in "${required_tools[@]}"; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required tool '$required' not found on PATH; refusing to report a gate that ran nothing" >&2
    exit 2
  fi
done

if (( TASK_ID >= FUZZSMT_SHARDS )); then
  echo "task $TASK_ID is outside the active $FUZZSMT_SHARDS-shard range"
  exit 0
fi

IFS=' ' read -r -a solver_words <<< "$SOLVER_BIN"
IFS=' ' read -r -a extra_args <<< "$SOLVER_EXTRA_ARGS"
model_solver_args=(--solver-arg=--quiet)
for extra in ${extra_args+"${extra_args[@]}"}; do
  model_solver_args+=("--solver-arg=$extra")
done
"${solver_words[0]}" --version

shard=$TASK_ID
seed=$((FUZZSMT_SEED_BASE + shard))
out="$OUT_ROOT/fuzzsmt/shard-$shard"
model_out="$OUT_ROOT/model-validation/shard-$shard"
mkdir -p "$out" "$model_out"
echo "task $TASK_ID: QF_LIA FuzzSMT shard $shard seed $seed"
python3 scripts/fuzz_external.py \
  --fuzzer fuzzsmt \
  --solver "$SOLVER_BIN --quiet $SOLVER_EXTRA_ARGS" \
  --refs z3 \
  --logic QF_LIA \
  --count "$FUZZSMT_COUNT" \
  --timeout "$FUZZSMT_TIMEOUT" \
  --seed "$seed" \
  --fuzzer-args "$FUZZSMT_ARGS" \
  --out "$out"
model_status=0
python3 scripts/validate_model.py \
  --solver "${solver_words[0]}" \
  "${model_solver_args[@]}" \
  --timeout "$MODEL_TIMEOUT" \
  --dolmen-timeout "$MODEL_DOLMEN_TIMEOUT" \
  --dolmen-arg=--strict=false \
  --out "$model_out" \
  "$out"/generated/*.smt2 || model_status=$?
if (( model_status != 0 )); then
  # See slurm_qf_lra_model_fuzz_gate.sbatch's own comment: a lone solver
  # *answer* timeout on a hard fuzzed instance is not a model-validation
  # failure. Only fail the task for something other than a bare timeout.
  non_timeout_failures=$(python3 -c "
import csv, sys
n = 0
with open(sys.argv[1], newline='') as fh:
    for row in csv.DictReader(fh):
        if row['outcome'] not in ('pass', 'not-sat', 'solver-timeout'):
            n += 1
print(n)
" "$model_out/summary.csv")
  if (( non_timeout_failures > 0 )); then
    echo "model validation found $non_timeout_failures non-timeout failure(s) in $model_out/summary.csv; failing the task" >&2
    exit "$model_status"
  fi
  echo "model validation's only failures were solver timeouts (benign under a ${MODEL_TIMEOUT}s solve budget); not failing the task" >&2
fi
