#!/usr/bin/env bash
#SBATCH --job-name=primo-build-fuzz
#SBATCH --cpus-per-task=8
#SBATCH --mem=8G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

# Rebuilds the remote Debug+ASAN binary the fuzz gates run against
# (build-slurm-fuzz), which slurm_prepare_benchmark_workspace.py does not
# touch -- it only builds build-slurm-rel.
#
# This has to be a batch job rather than a plain ssh command: the head node
# no longer carries libasan.so.6.0.0, so linking an ASAN build there fails
# with "cannot find /usr/lib64/libasan.so.6.0.0" while the same link
# succeeds on a compute node.
#
# Validate before submitting:
#   python3 scripts/slurm_validate_sbatch.py scripts/slurm_build_fuzz.sbatch
#
# Submit:
#   sbatch scripts/slurm_build_fuzz.sbatch

set -euo pipefail

# Default to the directory the job was submitted from, so two parallel
# work streams in two checkouts cannot land in the same tree. A shared
# hardcoded default was how a QF_LRA sync nearly clobbered the binary a
# running Nelson-Oppen job depended on (2026-07-31).
WORKDIR=${PRIMO_WORKDIR:-${SLURM_SUBMIT_DIR:-$PWD}}
CMAKE=${PRIMO_CMAKE:-.venv-build/bin/cmake}
BUILD_DIR=${PRIMO_FUZZ_BUILD_DIR:-build-slurm-fuzz}
JOBS=${PRIMO_BUILD_JOBS:-8}

cd "$WORKDIR"
"$CMAKE" --build "$BUILD_DIR" --target primo -j "$JOBS"
"$BUILD_DIR/primo" --version
