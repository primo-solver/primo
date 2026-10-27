#!/usr/bin/env bash
#SBATCH --job-name=primo-symmetry-mutations
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
root=results/symmetry-mutations
test ! -e "$root"
mkdir -p "$root/seeds"
exec > >(tee "$root/stdout.log") 2> >(tee "$root/stderr.log" >&2)
trap 'status=$?; printf "%s\n" "$status" > "$root/exit-code"' EXIT
build-slurm-rel/primo --version > "$root/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_REVISION:?}" "$root/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$root/executables.sha256"
cp benchmarks/smoke/qf_uflra/sat_symmetry_*.smt2 benchmarks/seeds/qf_uflra_scale/*.smt2 "$root/seeds/"
sha256sum "$root/seeds/"*.smt2 > "$root/seeds.sha256"
python3 scripts/check_solver_seed_corpus.py --seeds "$root/seeds" \
  --solver build-slurm-rel/primo --solver-arg=--quiet --ref contrib/local/bin/z3 \
  --timeout 30 --out "$root/seed-answers.csv"
scripts/yinyang_fuzz.sh --seeds "$root/seeds" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
  --config scripts/fuzz_configs/model_linear_ops.txt \
  --solver "$PWD/build-slurm-rel/primo --quiet" --ref 'z3 model_validate=true' \
  --seconds 300 --iterations 20 --solver-timeout 5 --file-size-limit 2000000 \
  --bugs "$root/bugs" --scratch "$root/scratch" --logs "$root/logs" \
  --random-seed 202610021 --keep-logic --hoist-declarations --keep-mutants
python3 scripts/check_solver_seed_corpus.py --seeds "$root/scratch" \
  --ignore-declared-status --solver build-slurm-rel/primo --solver-arg=--quiet \
  --ref contrib/local/bin/z3 --timeout 10 --out "$root/mutant-answers.csv"
for stage in seed mutant; do
  python3 scripts/select_fuzz_sat_inputs.py "$root/$stage-answers.csv" --out "$root/$stage-sat.txt"
  if [[ -s $root/$stage-sat.txt ]]; then
    python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet \
      --dolmen contrib/local/bin/dolmen --require-dolmen-pass --expect-sat \
      --dolmen-arg=--strict=false --timeout 30 --dolmen-timeout 30 \
      --out "$root/$stage-models" --manifest "$root/$stage-sat.txt"
  fi
done
if [[ -d $root/bugs ]] && [[ -n $(find "$root/bugs" -type f -print -quit) ]]; then
  echo 'saved mutation failures require classification' >&2
  exit 1
fi
