#!/usr/bin/env bash
#SBATCH --job-name=primo-public-checks
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:40:00
#SBATCH --output=results/public-checks-%j.out
#SBATCH --error=results/public-checks-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PATH"
command -v cmake
command -v ctest
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4 CTEST_TEST_TIMEOUT=120
if [[ -n ${PRIMO_CXX_HEADER_MANIFEST:-} ]]; then
  while IFS= read -r header; do
    [[ $header =~ ^[a-zA-Z0-9_./-]+$ ]] && test -d "$header"
    export CXXFLAGS="${CXXFLAGS:-} -isystem $header"
  done < "$PRIMO_CXX_HEADER_MANIFEST"
fi
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_REVISION:?}"
cpp_files=$(python3 -c 'from pathlib import Path; print(" ".join(str(p) for root in ("src", "tests") for p in sorted(Path(root).rglob("*")) if p.suffix in (".cpp", ".hpp", ".h")))')
make -o contrib-required check BUILD_DIR=build-slurm-rel CPP_FORMAT_FILES="$cpp_files"
python3 scripts/check_doc_examples.py --solver build-slurm-rel/primo
make -o contrib-required asan-test ASAN_BUILD_DIR=build-public-asan
sha256sum build-slurm-rel/primo
