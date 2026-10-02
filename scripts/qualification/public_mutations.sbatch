#!/usr/bin/env bash
# Ground, logic-preserving real-seed mutations; retain inputs for strict replay.
#SBATCH --job-name=primo-public-mutations
#SBATCH --array=0-1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
command -v opfuzz
slot=${SLURM_ARRAY_TASK_ID:?}
if (( slot == 0 )); then
  corpus=benchmarks/seeds/qf_lra_real; iterations=8
elif (( slot == 1 )); then
  corpus=benchmarks/seeds/qf_uflra_scale; iterations=50
else
  exit 2
fi
out="results/public-mutations/${SLURM_ARRAY_JOB_ID}/shard-$slot"
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/stdout.log") 2> >(tee "$out/stderr.log" >&2)
trap 'status=$?; printf "%s\n" "$status" > "$out/exit-code"' EXIT
set -x
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_REVISION:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/executables.sha256"
python3 -m pip show yinyang z3-solver > "$out/mutator-version.txt"
sha256sum "$corpus"/*.smt2 > "$out/seeds.sha256"
python3 scripts/check_solver_seed_corpus.py --seeds "$corpus" \
  --solver build-slurm-rel/primo --solver-arg=--quiet --ref contrib/local/bin/z3 \
  --timeout 30 --out "$out/seed-answers.csv"
scripts/yinyang_fuzz.sh --seeds "$corpus" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
  --config scripts/fuzz_configs/model_linear_ops.txt \
  --solver "$PWD/build-slurm-rel/primo --quiet" --ref 'z3 model_validate=true' \
  --seconds 900 --iterations "$iterations" --solver-timeout 5 --file-size-limit 2000000 \
  --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" \
  --random-seed "$((202610020 + slot))" --keep-logic --hoist-declarations --keep-mutants
answer_status=0
python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" \
  --ignore-declared-status --solver build-slurm-rel/primo --solver-arg=--quiet \
  --ref contrib/local/bin/z3 --timeout 10 --out "$out/mutant-answers.csv" || answer_status=$?
# Model replay uses only successful SAT inputs. Resource limits stay incomplete.
if (( answer_status != 0 )); then
  python3 scripts/classify_mutation_resources.py answers "$out/mutant-answers.csv"
fi
for stage in seed mutant; do
  python3 scripts/select_fuzz_sat_inputs.py "$out/$stage-answers.csv" --out "$out/$stage-sat.txt"
  if [[ -s "$out/$stage-sat.txt" ]]; then
    python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet \
      --dolmen contrib/local/bin/dolmen --require-dolmen-pass --expect-sat \
      --dolmen-arg=--strict=false --timeout 30 --dolmen-timeout 30 \
      --out "$out/$stage-models" --manifest "$out/$stage-sat.txt"
  fi
done
(( answer_status == 0 ))
if [[ -d $out/bugs ]] && [[ -n $(find "$out/bugs" -type f -print -quit) ]]; then
  echo 'saved mutation failures require classification' >&2
  exit 1
fi
