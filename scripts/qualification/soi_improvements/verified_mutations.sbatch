#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-verified-mutations
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:40:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
out="${PRIMO_SOI_MUTATION_OUT:?}"
mkdir -p "$out"
python3 scripts/prepare_soi_mutation_seeds.py --corpus "${PRIMO_SOI_CORPUS:?}" \
 --historical docs/coverage/lra-simplex-per-input-20261031.csv --out "$out/selection"
# Preserve the lexicographic copy order used by the qualified run.
mkdir "$out/seeds"
cp "$out/selection/"*.smt2 "$out/seeds/"
./build-slurm-rel/primo --version > "$out/version.txt"
sha256sum ./build-slurm-rel/primo > "$out/binary.sha256"
scripts/yinyang_fuzz.sh --config benchmarks/fuzz-configs/qf_lra_typefuzz.txt \
 --hoist-declarations --keep-logic --file-size-limit 200000 --keep-mutants \
 --seeds "$out/seeds" \
 --solver "$PWD/build-slurm-rel/primo --quiet --lra-pivoting-rule=soi --lra-soi-selection=short-column --lra-soi-pricing-candidates=64 --lra-soi-stall-limit=8 --lra-soi-cached-conflicts --lra-soi-linear-maintenance --lra-soi-minimize=row-growing --check-model" \
 --ref "$PWD/contrib/local/bin/z3 model_validate=true" \
 --seconds 900 --iterations 20 --solver-timeout 5 --random-seed 611821 \
 --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" > "$out/run.log" 2>&1
python3 scripts/classify_soi_mutations.py --mutants "$out/scratch" \
 --plan "${PRIMO_SOI_PLAN:?}" --solver build-slurm-rel/primo \
 --reference contrib/local/bin/z3 --out "$out/classification.json"
