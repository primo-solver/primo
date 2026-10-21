#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-equality-checks
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_DIR:?}:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4
mkdir -p "${PRIMO_CHECK_OUT:?}"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
if [[ ${PRIMO_CHECK_KIND:-release} == asan ]]; then
  cxx_header_flags=
  while IFS= read -r header_root; do
    [[ -d $header_root ]]
    cxx_header_flags+=" -isystem $header_root"
  done < "${PRIMO_CXX_HEADER_MANIFEST:?}"
  export CXXFLAGS="${CXXFLAGS:-}$cxx_header_flags"
  asan_config=(-DCMAKE_BUILD_TYPE=Debug -DPRIMO_SANITIZERS=address
    "-DCMAKE_PREFIX_PATH=$PWD/contrib/local"
    "-DPRIMO_GIT_REVISION_OVERRIDE=$PRIMO_EXPECTED_GIT")
  if [[ -n ${PRIMO_ASAN_RUNTIME_DIR:-} ]]; then
    test -f "$PRIMO_ASAN_RUNTIME_DIR/libasan.so"
    asan_config+=("-DCMAKE_EXE_LINKER_FLAGS=-L$PRIMO_ASAN_RUNTIME_DIR")
    export LD_LIBRARY_PATH="$PRIMO_ASAN_RUNTIME_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  fi
  cmake -S . -B build-slurm-asan "${asan_config[@]}"
  # Workspace preflight verifies these reusable dependency files. Some shared
  # installations lack Make's download stamps; do not download/rebuild them.
  make -o contrib/local/.cli11.stamp -o contrib/local/.cadical.stamp \
    -o contrib/local/.gmp.stamp asan-test ASAN_BUILD_DIR=build-slurm-asan \
    > "$PRIMO_CHECK_OUT/asan.log" 2>&1
else
  cmake --build build-slurm-rel > "$PRIMO_CHECK_OUT/build.log" 2>&1
  ctest --test-dir build-slurm-rel --timeout 180 --output-on-failure -j4 > "$PRIMO_CHECK_OUT/ctest.log" 2>&1
  python3 -m unittest discover -s tests/python -p 'test_*.py' > "$PRIMO_CHECK_OUT/python.log" 2>&1
  python3 scripts/check_doc_examples.py --solver build-slurm-rel/primo > "$PRIMO_CHECK_OUT/doc-examples.log" 2>&1
  PRIMO_PHASE=precheck python3 scripts/qualification/lra_equalities/campaign.py precheck > "$PRIMO_CHECK_OUT/seeds.log" 2>&1
fi
