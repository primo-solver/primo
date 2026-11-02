#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-includes
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=01:00:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
out="${PRIMO_REVIEW_OUT_DIR:?}"
mkdir -p "$out"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
./build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
args=()
while IFS= read -r root; do
  test -d "$root"
  args+=(--extra-arg=-isystem --extra-arg="$root")
done < contrib/local/clang-tools-env/cxx-headers/include-paths.txt
# The prepared archive contains only tracked source/test files.
find src tests -name '*.cpp' | sort > "$out/sources.txt"
mapfile -t sources < "$out/sources.txt"
clang-tidy -p build-slurm-rel -checks='-*,clang-diagnostic-*,misc-include-cleaner' \
  "${args[@]}" "${sources[@]}" > "$out/include-cleaner.log" 2>&1
