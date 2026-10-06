#!/usr/bin/env bash
#SBATCH --job-name=primo-integer-migration-checks
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
out="results/integer-migration-checks-${SLURM_JOB_ID}"
mkdir -p "$out"
rm -rf build-public-asan build-integer-scaffold
bash scripts/qualification/public_checks.sbatch > "$out/full-checks.log" 2>&1
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PATH"
if [[ -n ${PRIMO_CXX_HEADER_MANIFEST:-} ]]; then
  while IFS= read -r header; do
    [[ $header =~ ^[a-zA-Z0-9_./-]+$ ]] && test -d "$header"
    export CXXFLAGS="${CXXFLAGS:-} -isystem $header"
  done < "$PRIMO_CXX_HEADER_MANIFEST"
fi
cmake -S . -B build-integer-scaffold -DCMAKE_BUILD_TYPE=Release \
  -DIPASIR_UP_INCLUDE_DIR=OFF -DCADICAL_LIBRARY=OFF \
  "-DPRIMO_GIT_REVISION_OVERRIDE=${PRIMO_EXPECTED_REVISION:?}" > "$out/scaffold.log" 2>&1
cmake --build build-integer-scaffold -j4 >> "$out/scaffold.log" 2>&1
build-integer-scaffold/primo --version >> "$out/scaffold.log" 2>&1
ctest --test-dir build-integer-scaffold --output-on-failure \
  -R '^primo_(core|rational|lia_completion)_tests$' >> "$out/scaffold.log" 2>&1
