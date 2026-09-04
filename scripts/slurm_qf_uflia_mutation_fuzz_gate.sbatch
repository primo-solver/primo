#!/usr/bin/env bash
# Time-boxed QF_UFLIA real-seed mutation gate using YinYang/typefuzz.
#
# Prepare the remote workspace on the login node with contrib-yinyang,
# contrib-z3 and contrib-dolmen.  Cluster-specific paths and run identifiers are supplied through
# the environment at submission time.
#SBATCH --job-name=primo-qfuflia-mut
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the prepared remote workspace}
SEEDS=${PRIMO_FUZZ_YINYANG_SEEDS:-"$WORKDIR/tmp/QF_UFLIA/mathsat"}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/qf-uflia-mutation-${SLURM_JOB_ID:-manual}"}
WALL_SECONDS=${PRIMO_FUZZ_YINYANG_WALL_SECONDS:-600}
ITERATIONS=${PRIMO_FUZZ_YINYANG_ITERATIONS:-20}
SOLVER_TIMEOUT=${PRIMO_FUZZ_SOLVER_TIMEOUT:-2}
FILE_SIZE_LIMIT=${PRIMO_FUZZ_FILE_SIZE_LIMIT:-20000}
RANDOM_SEED=${PRIMO_FUZZ_RANDOM_SEED:-202609030}
SOLVER_ARGS=${PRIMO_FUZZ_SOLVER_ARGS:-}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"
PATH="$WORKDIR/contrib/local/bin:$WORKDIR/.venv-yinyang/bin:$PATH"
export PATH

for required in typefuzz z3 dolmen; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required tool '$required' not found; prepare contrib-yinyang, contrib-z3 and contrib-dolmen first" >&2
    exit 2
  fi
done
if [[ ! -e "$SEEDS" ]]; then
  echo "QF_UFLIA mutation seed path does not exist: $SEEDS" >&2
  exit 2
fi

./build-slurm-rel/primo --version
z3 --version

solver_command="$WORKDIR/build-slurm-rel/primo --quiet"
if [[ -n $SOLVER_ARGS ]]; then
  solver_command+=" $SOLVER_ARGS"
fi

scripts/yinyang_fuzz.sh \
  --seeds "$SEEDS" \
  --solver "$solver_command" \
  --ref "$WORKDIR/contrib/local/bin/z3 model_validate=true" \
  --seconds "$WALL_SECONDS" \
  --iterations "$ITERATIONS" \
  --solver-timeout "$SOLVER_TIMEOUT" \
  --bugs "$OUT_ROOT/bugs" \
  --scratch "$OUT_ROOT/scratch" \
  --logs "$OUT_ROOT/logs" \
  --file-size-limit "$FILE_SIZE_LIMIT" \
  --random-seed "$RANDOM_SEED" \
  --keep-logic \
  --hoist-declarations \
  --keep-mutants

# The differential run above compares answers only: it never asks Primo for a
# model, so it does not reach model construction at all. That left model
# construction covered only by the generated gate, whose cases have a bounded
# term shape, and a crash in integer model completion on nested applications
# survived both. Re-running the retained mutants -- which carry real benchmark
# structure -- through the model validator closes that gap.
mutants=("$OUT_ROOT"/scratch/*.smt2)
if [[ -e ${mutants[0]} ]]; then
  echo "validating models for ${#mutants[@]} retained mutants"
  python3 scripts/validate_model.py \
    --solver "$WORKDIR/build-slurm-rel/primo" \
    --solver-arg=--quiet \
    --require-dolmen-pass \
    --timeout "$SOLVER_TIMEOUT" \
    --dolmen-timeout 5 \
    --dolmen-arg=--strict=false \
    --out "$OUT_ROOT/model-validation" \
    "${mutants[@]}"
else
  echo "no retained mutants to validate under $OUT_ROOT/scratch" >&2
fi
