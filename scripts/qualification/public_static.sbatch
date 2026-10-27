#!/usr/bin/env bash
#SBATCH --job-name=primo-public-static
#SBATCH --cpus-per-task=8
#SBATCH --mem=16G
#SBATCH --time=00:45:00
#SBATCH --output=results/public-static-%j.out
#SBATCH --error=results/public-static-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
python3 -c 'from pathlib import Path; print("\n".join(str(p) for root in ("src", "tests") for p in sorted(Path(root).rglob("*.cpp"))))' > results/public-static-files.txt
xargs -a results/public-static-files.txt -n1 -P8 clang-tidy -p build-slurm-rel > results/public-tidy.log 2>&1
xargs -a results/public-static-files.txt -n1 -P8 clang-tidy -p build-slurm-rel \
  -checks='-*,clang-diagnostic-*,misc-include-cleaner' > results/public-includes.log 2>&1
