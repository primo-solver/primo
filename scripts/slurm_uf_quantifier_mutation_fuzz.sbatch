#!/usr/bin/env bash
# Real-seed mutation fuzzing of the quantified UF route (docs/quantifiers.md,
# M4) with YinYang/typefuzz against Z3.
#
# Seeds are the smallest files of every family of the SMT-LIB UF division,
# rebuilt per task by build_fuzz_seed_corpus.py. Answers only: quantified input
# has no model route, so unlike the QF_UFLIA gate there is no model stage.
# Each array task uses its own typefuzz random seed.
#
# Prepare the workspace with contrib-yinyang and contrib-z3. Stage the UF
# corpus and point PRIMO_CORPUS_ROOT at its family directories.
# PRIMO_FUZZ_SOLVER_ARGS is a single line of whitespace-separated option tokens.
#SBATCH --job-name=primo-uf-quant-mut
#SBATCH --array=0-3
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:40:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the prepared remote workspace}
CORPUS_ROOT=${PRIMO_CORPUS_ROOT:-"$HOME/primo-quantifiers-data/non-incremental/UF"}
TASK=${SLURM_ARRAY_TASK_ID:-0}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/uf-quantifier-mutation-${SLURM_ARRAY_JOB_ID:-manual}"}/task-$TASK
PER_FAMILY=${PRIMO_FUZZ_SEEDS_PER_FAMILY:-3}
FILE_SIZE_LIMIT=${PRIMO_FUZZ_FILE_SIZE_LIMIT:-20000}
WALL_SECONDS=${PRIMO_FUZZ_YINYANG_WALL_SECONDS:-1800}
ITERATIONS=${PRIMO_FUZZ_YINYANG_ITERATIONS:-50}
SOLVER_TIMEOUT=${PRIMO_FUZZ_SOLVER_TIMEOUT:-5}
RANDOM_SEED_BASE=${PRIMO_FUZZ_RANDOM_SEED:-202609110}
SOLVER_ARGS=${PRIMO_FUZZ_SOLVER_ARGS:-}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"
PATH="$WORKDIR/contrib/local/bin:$WORKDIR/.venv-yinyang/bin:$PATH"
export PATH

for required in typefuzz z3; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required tool '$required' not found; prepare contrib-yinyang and contrib-z3 first" >&2
    exit 2
  fi
done
if [[ ! -d "$CORPUS_ROOT" ]]; then
  echo "UF corpus root does not exist: $CORPUS_ROOT" >&2
  exit 2
fi

./build-slurm-rel/primo --version | tee "$OUT_ROOT/version.txt"
z3 --version | tee -a "$OUT_ROOT/version.txt"

python3 scripts/build_fuzz_seed_corpus.py --root "$CORPUS_ROOT" \
  --out "$OUT_ROOT/seeds" --per-family "$PER_FAMILY" --max-bytes "$FILE_SIZE_LIMIT" \
  | tee "$OUT_ROOT/seed-families.txt"

solver_command="$WORKDIR/build-slurm-rel/primo --quiet"
if [[ -n $SOLVER_ARGS ]]; then
  solver_command+=" $SOLVER_ARGS"
fi

scripts/yinyang_fuzz.sh \
  --seeds "$OUT_ROOT/seeds" \
  --solver "$solver_command" \
  --ref "$WORKDIR/contrib/local/bin/z3" \
  --seconds "$WALL_SECONDS" \
  --iterations "$ITERATIONS" \
  --solver-timeout "$SOLVER_TIMEOUT" \
  --bugs "$OUT_ROOT/bugs" \
  --scratch "$OUT_ROOT/scratch" \
  --logs "$OUT_ROOT/logs" \
  --file-size-limit "$FILE_SIZE_LIMIT" \
  --random-seed "$((RANDOM_SEED_BASE + TASK))" \
  --keep-logic \
  --hoist-declarations \
  --keep-mutants
