#!/usr/bin/env bash
# Historical exploratory dispatch. Task0 needs an opfuzz supporting --seed.
# Use verified_mutations.sbatch for retained-input correctness qualification.
#SBATCH --job-name=primo-soi-mutations
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=01:00:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
task=${SLURM_ARRAY_TASK_ID:?}
out="$PWD/fuzz-results/final-mutations/task-$task"
mkdir -p "$out"
python3 scripts/prepare_soi_mutation_seeds.py --corpus "${PRIMO_SOI_CORPUS:?}" \
 --historical docs/coverage/lra-simplex-per-input-20261031.csv --out "$out/seeds"
extra=()
if [[ $task == 0 ]]; then
 extra+=(--typefuzz opfuzz --config benchmarks/fuzz-configs/qf_lra_operator_mutations.txt)
else
 extra+=(--config benchmarks/fuzz-configs/qf_lra_typefuzz.txt --hoist-declarations)
fi
./build-slurm-rel/primo --version > "$out/version.txt"
sha256sum ./build-slurm-rel/primo > "$out/binary.sha256"
scripts/yinyang_fuzz.sh "${extra[@]}" --keep-logic --seeds "$out/seeds" \
 --solver "$PWD/build-slurm-rel/primo --quiet --lra-pivoting-rule=soi --lra-soi-selection=short-column --lra-soi-pricing-candidates=64 --lra-soi-stall-limit=8 --lra-soi-cached-conflicts --lra-soi-linear-maintenance --lra-soi-minimize=row-growing --check-model" \
 --ref "$PWD/contrib/local/bin/z3 model_validate=true" --seconds 1500 --iterations 100 \
 --solver-timeout 5 --file-size-limit 200000 --random-seed "$((611820 + task))" \
 --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" > "$out/run.log" 2>&1
