#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-check
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
[[ -n ${SLURM_JOB_ID:-} ]]
./build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
mkdir -p "${PRIMO_CHECK_OUT:?}"
.venv-build/bin/cmake --build build-slurm-rel -j4 > "$PRIMO_CHECK_OUT/build.log" 2>&1
.venv-build/bin/ctest --test-dir build-slurm-rel --timeout 300 --output-on-failure > "$PRIMO_CHECK_OUT/ctest.log" 2>&1
python3 -m unittest discover -s tests/python -p 'test_*.py' > "$PRIMO_CHECK_OUT/python.log" 2>&1
for profile in arithmetic both; do
  flags=(--solver-arg=--quiet --solver-arg=--preprocessing-mode=off)
  if [[ $profile == both ]]; then flags+=(--solver-arg=--qf-uflra-online-euf --solver-arg=--euf-prop=online); fi
  for seeds in qf_uflra qf_uflra_scale; do
    python3 scripts/check_solver_seed_corpus.py --seeds "benchmarks/seeds/$seeds" \
      --solver ./build-slurm-rel/primo "${flags[@]}" --ref contrib/local/bin/z3 \
      --timeout 10 --out "$PRIMO_CHECK_OUT/$profile-$seeds.csv"
  done
done
