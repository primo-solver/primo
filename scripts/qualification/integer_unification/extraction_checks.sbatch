#!/usr/bin/env bash
#SBATCH --job-name=primo-integer-extraction-checks
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
out="results/integer-extraction-checks-${SLURM_JOB_ID}"
mkdir -p "$out"
# The prepared Release build is clean. Also prevent an older ASan tree from
# reusing objects across a source snapshot or shared-filesystem clock skew.
rm -rf build-public-asan
bash scripts/qualification/public_checks.sbatch > "$out/full-checks.log" 2>&1
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
for file in src/smt/solver_engines.cpp src/theory/arithmetic/lia_completion.cpp \
            tests/unit/lia_completion_tests.cpp tests/unit/instance_simplifier_tests.cpp; do
  clang-tidy -p build-slurm-rel "$file" >> "$out/tidy.log" 2>&1
  clang-tidy -p build-slurm-rel -checks='-*,clang-diagnostic-*,misc-include-cleaner' \
    "$file" >> "$out/includes.log" 2>&1
done
