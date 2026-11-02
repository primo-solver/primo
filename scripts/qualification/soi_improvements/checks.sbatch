#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-improvement-checks
#SBATCH --cpus-per-task=4
#SBATCH --mem=12G
#SBATCH --time=01:00:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
out="${PRIMO_CHECK_OUT:?}"
mkdir -p "$out"
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PATH"
./build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
# Reused installations were verified by the preparation preflight. Refresh
# stamps only in this candidate workspace, never in the frozen baseline.
for dependency in cli11 cadical gmp; do
  touch -r "contrib/install_$dependency.py" "contrib/local/.$dependency.stamp"
done
make -j4 check BUILD_DIR=build-slurm-rel > "$out/check.log" 2>&1
python3 scripts/check_soi_explanations.py --fixtures build-slurm-rel/soi-explanations \
  --reference contrib/local/bin/z3 --out "$out/explanations.json"
sha256sum build-slurm-rel/primo > "$out/primo.sha256"
