#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-asan
#SBATCH --cpus-per-task=4
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
mkdir -p "${PRIMO_ASAN_OUT:?}"
./build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
# Use a complete compiler installation when the cluster's system headers are
# provided separately. These paths are supplied by the execution environment.
export PATH="${PRIMO_ASAN_TOOLCHAIN:?}/bin:$PWD/.venv-build/bin:$PATH"
export LD_LIBRARY_PATH="$PRIMO_ASAN_TOOLCHAIN/lib64:${LD_LIBRARY_PATH:-}"
export CC=gcc CXX=g++
export CXXFLAGS="-idirafter ${PRIMO_C_SYSTEM_HEADERS:?}"
cmake -S . -B build-soi-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$PWD/contrib/local" -DPRIMO_SANITIZERS=address \
  -DPRIMO_GIT_REVISION_OVERRIDE="$PRIMO_EXPECTED_GIT" > "$PRIMO_ASAN_OUT/configure.log" 2>&1
make -j4 asan-test ASAN_BUILD_DIR=build-soi-asan > "$PRIMO_ASAN_OUT/asan-test.log" 2>&1
