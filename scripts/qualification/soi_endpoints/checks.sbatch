#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-check
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
OUT=${PRIMO_CHECK_OUT:?}
mkdir -p "$OUT"
export PATH="$PWD/.venv-build/bin:$PWD/contrib/local/clang-tools-env/bin:$PATH"
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$OUT/version.txt"
make -j4 check BUILD_DIR=build-slurm-rel > "$OUT/check.log" 2>&1
for seeds in benchmarks/seeds/qf_lra benchmarks/smoke/qf_lia benchmarks/seeds/qf_uflra; do
  logic=${seeds##*/}
  for policy in soi sparse; do
    python3 scripts/check_solver_seed_corpus.py --seeds "$seeds" \
      --solver ./build-slurm-rel/primo --solver-arg=--quiet \
      --solver-arg="--lra-pivoting-rule=$policy" --ref contrib/local/bin/z3 \
      --timeout 10 --out "$OUT/$logic-$policy.csv"
  done
done
python3 scripts/check_solver_seed_corpus.py --seeds benchmarks/regressions/soi \
  --solver ./build-slurm-rel/primo --solver-arg=--quiet \
  --solver-arg=--lra-pivoting-rule=soi --solver-arg=--preprocessing-mode=off \
  --ref contrib/local/bin/z3 --timeout 10 --out "$OUT/regressions.csv"
# The public download exercises the same component without preprocessing.
header_args=()
if [[ -n ${PRIMO_CXX_HEADER_MANIFEST:-} ]]; then
  while IFS= read -r root; do
    test -d "$root"
    header_args+=(-isystem "$root")
  done < "$PRIMO_CXX_HEADER_MANIFEST"
fi
c++ -std=c++20 -O0 "${header_args[@]}" -Isrc -Icontrib/local/include \
  docs-next/examples/soi-zero-step.cpp build-slurm-rel/libprimo_theory.a \
  build-slurm-rel/libprimo_util.a contrib/local/lib/libgmpxx.a \
  contrib/local/lib/libgmp.a -o "$OUT/soi-zero-step"
timeout 10 "$OUT/soi-zero-step" > "$OUT/soi-zero-step.txt"
