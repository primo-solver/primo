#!/usr/bin/env bash
# A seed directory is supplied explicitly; the same profile reaches answers,
# original-seed models, and every retained mutant's printed model.
#SBATCH --job-name=primo-typed-mutation
#SBATCH --output=slurm_logs/primo-typed-mutation-%A_%a.out
#SBATCH --error=slurm_logs/primo-typed-mutation-%A_%a.err
#SBATCH --array=0-3%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:20:00

set -euo pipefail
cd "${PRIMO_WORKDIR:?set PRIMO_WORKDIR}"
export PATH="$PWD/contrib/local/bin:$PWD/.venv-yinyang/bin:$PATH"
SEEDS=${PRIMO_MODEL_SEEDS:?set PRIMO_MODEL_SEEDS}
TASK=${SLURM_ARRAY_TASK_ID:-0}
OUT="${PRIMO_FUZZ_OUT_DIR:?set PRIMO_FUZZ_OUT_DIR}/task-$TASK"
TIMEOUT=${PRIMO_MUTATION_TIMEOUT:-5}
MUTATOR=${PRIMO_MUTATION_TOOL:-typefuzz}
modes=(off basic strong auto)
(( TASK >= 0 && TASK < 4 ))
MODE=${modes[TASK]}
mkdir -p "$OUT"
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?set PRIMO_EXPECTED_GIT}" "$OUT/version.txt"
for required in "$MUTATOR" z3 dolmen; do command -v "$required"; done
options=(--quiet --check-model --preprocessing-mode="$MODE")
if [[ -n ${PRIMO_EXTRA_SOLVER_ARGS:-} ]]; then
  read -r -a extra <<< "$PRIMO_EXTRA_SOLVER_ARGS"
  options+=("${extra[@]}")
fi
solver_args=()
for arg in "${options[@]}"; do solver_args+=(--solver-arg="$arg"); done
printf '%s\n' "${options[@]}" > "$OUT/options.txt"
python3 scripts/check_solver_seed_corpus.py --seeds "$SEEDS" \
  --solver ./build-slurm-rel/primo "${solver_args[@]}" --ref z3 \
  --timeout "$TIMEOUT" --out "$OUT/seed-answers.csv"
mapfile -d '' -t originals < <(find "$SEEDS" -type f -name '*.smt2' -print0 | sort -z)
(( ${#originals[@]} > 0 ))
python3 scripts/validate_model.py --solver ./build-slurm-rel/primo "${solver_args[@]}" \
  --require-dolmen-pass --dolmen-arg=--strict=false --timeout "$TIMEOUT" \
  --dolmen-timeout 5 --out "$OUT/seed-models" "${originals[@]}"

mutation_args=(--typefuzz "$MUTATOR")
if [[ $MUTATOR == opfuzz ]]; then
  mutation_args=(--typefuzz "$PWD/scripts/seeded_opfuzz.py"
                 --config scripts/fuzz_configs/model_linear_ops.txt)
fi
printf '%s\n' "$MUTATOR" "${mutation_args[@]}" > "$OUT/mutator.txt"
scripts/yinyang_fuzz.sh --seeds "$SEEDS" "${mutation_args[@]}" \
  --solver "$PWD/build-slurm-rel/primo ${options[*]}" --ref 'z3 model_validate=true' \
  --seconds "${PRIMO_MUTATION_SECONDS:-120}" --iterations "${PRIMO_MUTATION_ITERATIONS:-20}" \
  --solver-timeout "$TIMEOUT" --file-size-limit "${PRIMO_MUTATION_SIZE_LIMIT:-2000000}" \
  --bugs "$OUT/bugs" --scratch "$OUT/scratch" --logs "$OUT/logs" \
  --random-seed "$((202609500 + TASK))" --keep-logic --hoist-declarations --keep-mutants
mutants=("$OUT"/scratch/*.smt2)
[[ -e ${mutants[0]} ]]
# YinYang's own comparison can accept an answer followed by an unrecognised
# nonzero exit. Recheck every retained input using the status-aware comparator.
answer_status=0
python3 scripts/check_solver_seed_corpus.py --seeds "$OUT/scratch" \
  --solver ./build-slurm-rel/primo "${solver_args[@]}" --ref z3 \
  --timeout "$TIMEOUT" --out "$OUT/mutant-answers.csv" || answer_status=$?
model_status=0
python3 scripts/validate_model.py --solver ./build-slurm-rel/primo "${solver_args[@]}" \
  --require-dolmen-pass --dolmen-arg=--strict=false --timeout "$TIMEOUT" \
  --dolmen-timeout 5 --out "$OUT/mutant-models" "${mutants[@]}" || model_status=$?
if (( answer_status != 0 || model_status != 0 )); then exit 1; fi
# A wall-terminated differential runner may return success with saved bugs.
# Never accept those until they have been independently classified.
if [[ -d $OUT/bugs ]] && [[ -n $(find "$OUT/bugs" -type f -print -quit) ]]; then
  echo 'saved mutation failures require classification' >&2
  exit 1
fi
