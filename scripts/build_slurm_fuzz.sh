#!/usr/bin/env bash
# Build the remote Debug+ASAN+UBSan binary (build-slurm-fuzz) that the fuzz
# gates run against. slurm_prepare_benchmark_workspace.py does not touch it --
# that only builds build-slurm-rel.
#
# Run this ON THE HEAD NODE, not as a batch job. That is the opposite of what
# scripts/slurm_build_fuzz.sbatch used to do, and the reason is worth stating
# because the previous diagnosis had it backwards (docs/run-log.json,
# "head node lacks libasan, compute nodes lack C++ headers"):
#
#   * Compute nodes have no C++ standard headers at all. `#include <vector>`
#     fails there with the system compiler, sanitizers or not, so they cannot
#     compile anything -- which makes them the wrong place to build.
#   * The head node's system gcc ships a broken libasan: the linker script
#     /usr/lib/gcc/x86_64-redhat-linux/11/libasan.so reads
#     `INPUT ( /usr/lib64/libasan.so.6.0.0 )` and that file does not exist.
#     Being an absolute path inside the script, -L cannot redirect it.
#
# Both are fixed by using the EasyBuild GCCcore toolchain instead of the
# system compiler: it carries its own headers and its own libasan, and its
# linker script points inside its own tree. Verified 2026-08-03 -- configure,
# build, and a compute-node run all succeed.
#
# The compute nodes still need the sanitizer runtime at *execution* time, so
# whatever runs the binary must put GCCCORE_LIB on LD_LIBRARY_PATH. The fuzz
# gate does this itself; print_run_hint below is the reminder for ad-hoc use.
#
#   ssh <cluster> 'cd primo-qflra && scripts/build_slurm_fuzz.sh'

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
CMAKE=${PRIMO_CMAKE:-.venv-build/bin/cmake}
BUILD_DIR=${PRIMO_FUZZ_BUILD_DIR:-build-slurm-fuzz}
JOBS=${PRIMO_BUILD_JOBS:-8}
GCCCORE=${PRIMO_GCCCORE_MODULE:-GCCcore/11.3.0}
GCCCORE_LIB=${PRIMO_GCCCORE_LIB:-/export/eb/software/GCCcore/11.3.0/lib64}
SANITIZERS=${PRIMO_SANITIZERS:-address;undefined}

cd "$WORKDIR"

# shellcheck disable=SC1091
source /etc/profile.d/modules.sh 2>/dev/null || true
if ! module load "$GCCCORE" 2>/dev/null; then
  echo "could not load $GCCCORE; the system compiler cannot link ASAN here" >&2
  echo "available: $(module avail GCCcore 2>&1 | tr '\n' ' ')" >&2
  exit 2
fi
echo "compiler: $(g++ --version | head -1)"

"$CMAKE" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DPRIMO_SANITIZERS="$SANITIZERS"
"$CMAKE" --build "$BUILD_DIR" --target primo -j "$JOBS"

# Fail loudly rather than shipping a binary with no sanitizers: an ASAN gate
# that silently ran an unsanitized build is worse than no gate, because it
# reports clean.
banner=$("$BUILD_DIR/primo" --version 2>&1)
echo "$banner"
if ! grep -q "sanitizers: address" <<<"$banner"; then
  echo "built binary reports no address sanitizer; refusing to call this an ASAN build" >&2
  exit 3
fi

echo
echo "Built $BUILD_DIR. Compute nodes need the sanitizer runtime at run time:"
echo "  export LD_LIBRARY_PATH=$GCCCORE_LIB:\$LD_LIBRARY_PATH"
