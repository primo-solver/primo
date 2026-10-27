#!/usr/bin/env bash
#SBATCH --job-name=primo-followup-examples-static
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --output=results/public-examples-static-%j.out
#SBATCH --error=results/public-examples-static-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
headers=()
while IFS= read -r header; do
  [[ $header =~ ^[a-zA-Z0-9_./-]+$ ]] && test -d "$header"
  headers+=(-isystem "$header")
done < "${PRIMO_CXX_HEADER_MANIFEST:?}"
for source in docs/examples/*.cpp; do
  printf 'SOURCE %s\n' "$source"
  clang-tidy "$source" -- -std=c++20 -Isrc -Icontrib/local/include "${headers[@]}"
  clang-tidy "$source" -checks='-*,clang-diagnostic-*,misc-include-cleaner' \
    -- -std=c++20 -Isrc -Icontrib/local/include "${headers[@]}"
done
