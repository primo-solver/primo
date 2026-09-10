#!/usr/bin/env bash
# Bounded per-file scans retain verdicts even if an allocation expires. Set
# START=0 if the previous multi-file clang-tidy invocation did not flush verdicts.
#SBATCH --job-name=primo-lexical-static
#SBATCH --array=0-3
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:20:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?prepared workspace}"
BASE=${PRIMO_STATIC_BASE:?original static artifact}
START=${PRIMO_STATIC_START:?zero-based first incomplete translation unit}
[[ "$START" =~ ^[0-9]+$ ]]
OUT="$BASE/resume-$SLURM_ARRAY_TASK_ID"
mkdir "$OUT"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_REVISION:?expected revision}" "$OUT/version.txt"
mapfile -t sources < "$BASE/sources.txt"
(( START < ${#sources[@]} ))
header_args=()
while IFS= read -r root; do
  test -d "$root"
  header_args+=(--extra-arg=-isystem --extra-arg="$root")
done < contrib/local/clang-tools-env/cxx-headers/include-paths.txt
tidy_sources=()
include_sources=()
for ((i=SLURM_ARRAY_TASK_ID; i<${#sources[@]}; i+=4)); do
  include_sources+=("${sources[i]}")
  if (( i >= START )); then tidy_sources+=("${sources[i]}"); fi
done
printf '%s\n' "${tidy_sources[@]}" > "$OUT/tidy-sources.txt"
printf '%s\n' "${include_sources[@]}" > "$OUT/include-sources.txt"
for ((i=SLURM_ARRAY_TASK_ID; i<${#sources[@]}; i+=4)); do
  if (( i >= START )); then
    clang-tidy -p build-slurm-rel "${header_args[@]}" "${sources[i]}" > "$OUT/tidy-$i.log" 2>&1
    printf '%s\n' "${sources[i]}" >> "$OUT/tidy-completed.txt"
  fi
  clang-tidy -p build-slurm-rel -checks='-*,clang-diagnostic-*,misc-include-cleaner' \
    "${header_args[@]}" "${sources[i]}" > "$OUT/include-$i.log" 2>&1
  printf '%s\n' "${sources[i]}" >> "$OUT/include-completed.txt"
done
