#!/usr/bin/env bash
# Separate instrumented build; never replace the retained timing binaries.
#SBATCH --job-name=primo-component-profile
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
OUT=${PRIMO_PROFILE_OUT:?}
CMAKE=${PRIMO_PROFILE_CMAKE:?}
./build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
test ! -e "$OUT/build"
mkdir -p "$OUT"
hostname > "$OUT/node.txt"
sha256sum src/smt/component_solver.cpp src/smt/component_dispatcher.cpp \
  src/smt/component_model_coordinator.cpp src/smt/solver_engines.cpp > "$OUT/sources.sha256"
flags='-pg -g -fno-omit-frame-pointer'
while IFS= read -r directory; do
  test -d "$directory"
  flags="$flags -isystem $directory"
done < "${PRIMO_CXX_HEADER_MANIFEST:?}"
CXXFLAGS="$flags" "$CMAKE" -S . -B "$OUT/build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXE_LINKER_FLAGS=-pg -DPRIMO_GIT_REVISION_OVERRIDE="${PRIMO_EXPECTED_GIT}-profile"
"$CMAKE" --build "$OUT/build" --target primo -j4
"$OUT/build/primo" --version > "$OUT/version.txt"
for index in 0 1 2 3 4; do
  mkdir -p "$OUT/run-$index"
  (
    cd "$OUT/run-$index"
    timeout 60 "$OUT/build/primo" --quiet --stats "${PRIMO_PROFILE_INPUT:?}" \
      > solver.stdout 2> solver.stderr
    test -s gmon.out
  )
done
gprof -b -p "$OUT/build/primo" "$OUT"/run-*/gmon.out > "$OUT/flat-profile.txt"
gprof -b -q "$OUT/build/primo" "$OUT"/run-*/gmon.out > "$OUT/call-graph.txt"
