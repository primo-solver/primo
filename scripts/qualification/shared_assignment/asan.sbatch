#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-asan
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
[[ -n ${SLURM_JOB_ID:-} ]]
./build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
mkdir -p "${PRIMO_ASAN_OUT:?}"
toolchain=${PRIMO_ASAN_TOOLCHAIN:?}
test -x "$toolchain/bin/g++"
export PATH="$toolchain/bin:$PATH"
export LD_LIBRARY_PATH="$toolchain/lib64:${LD_LIBRARY_PATH:-}"
test -f "${PRIMO_C_SYSTEM_HEADERS:?}/features.h"
export CXXFLAGS="-idirafter $PRIMO_C_SYSTEM_HEADERS"
export CC=gcc CXX=g++
.venv-build/bin/cmake -S . -B build-shared-asan-gcccore-complete -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$PWD/contrib/local" -DPRIMO_SANITIZERS=address \
  -DPRIMO_GIT_REVISION_OVERRIDE="$PRIMO_EXPECTED_GIT" > "$PRIMO_ASAN_OUT/configure.log" 2>&1
.venv-build/bin/cmake --build build-shared-asan-gcccore-complete -j4 > "$PRIMO_ASAN_OUT/build.log" 2>&1
ASAN_OPTIONS=detect_leaks=1:strict_init_order=1 .venv-build/bin/ctest \
  --test-dir build-shared-asan-gcccore-complete --timeout 600 --output-on-failure > "$PRIMO_ASAN_OUT/tests.log" 2>&1
