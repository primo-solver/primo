#!/usr/bin/env bash
# Whole-source review of the clean snapshot prepared by the repository helper.
#SBATCH --job-name=primo-model-static
#SBATCH --output=slurm_logs/primo-model-static-%j.out
#SBATCH --error=slurm_logs/primo-model-static-%j.err
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:30:00

set -euo pipefail
cd "${PRIMO_WORKDIR:?set PRIMO_WORKDIR}"
OUT=${PRIMO_REVIEW_OUT_DIR:?set PRIMO_REVIEW_OUT_DIR}
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
mkdir -p "$OUT"
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?set PRIMO_EXPECTED_GIT}" "$OUT/version.txt"
clang-tidy --version > "$OUT/clang-tidy-version.txt"
# CMake generates compile commands for every target even when the preparation
# helper builds only Primo. Do not rebuild using a different compute-node GCC.
mapfile -t header_roots < contrib/local/clang-tools-env/cxx-headers/include-paths.txt
header_args=()
for root in "${header_roots[@]}"; do
  test -d "$root"
  header_args+=(--extra-arg=-isystem --extra-arg="$root")
done
cp contrib/local/clang-tools-env/cxx-headers/compiler-search.txt "$OUT/compiler-search.txt"
if command -v rg >/dev/null 2>&1; then
  rg --files src tests -g '*.cpp' -g '!src/smtlib/antlr/**' | sort > "$OUT/sources.txt"
else
  find src tests -name '*.cpp' -not -path 'src/smtlib/antlr/*' | sort > "$OUT/sources.txt"
fi
mapfile -t sources < "$OUT/sources.txt"
(( ${#sources[@]} > 0 ))
clang-tidy -p build-slurm-rel "${header_args[@]}" "${sources[@]}" > "$OUT/tidy.log" 2>&1
clang-tidy -p build-slurm-rel -checks='-*,clang-diagnostic-*,misc-include-cleaner' \
  "${header_args[@]}" "${sources[@]}" > "$OUT/include-cleaner.log" 2>&1
echo "reviewed ${#sources[@]} non-generated C++ files; advisory include findings require inspection"
