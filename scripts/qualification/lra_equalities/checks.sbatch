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
  make asan-test ASAN_BUILD_DIR=build-slurm-asan > "$PRIMO_CHECK_OUT/asan.log" 2>&1
else
  cmake --build build-slurm-rel > "$PRIMO_CHECK_OUT/build.log" 2>&1
  ctest --test-dir build-slurm-rel --timeout 180 --output-on-failure -j4 > "$PRIMO_CHECK_OUT/ctest.log" 2>&1
  python3 -m unittest discover -s tests/python -p 'test_*.py' > "$PRIMO_CHECK_OUT/python.log" 2>&1
  python3 scripts/check_doc_examples.py --solver build-slurm-rel/primo > "$PRIMO_CHECK_OUT/doc-examples.log" 2>&1
  PRIMO_PHASE=precheck python3 scripts/qualification/lra_equalities/campaign.py precheck > "$PRIMO_CHECK_OUT/seeds.log" 2>&1
fi
