#!/usr/bin/env bash
# Strictly conforming Boolean/arithmetic models, including monotone replay.
#SBATCH --job-name=primo-typed-lra-replay
#SBATCH --output=slurm_logs/primo-typed-lra-replay-%A_%a.out
#SBATCH --error=slurm_logs/primo-typed-lra-replay-%A_%a.err
#SBATCH --array=0-6%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:15:00

set -euo pipefail
cd "${PRIMO_WORKDIR:?set PRIMO_WORKDIR}"
export PATH="$PWD/contrib/local/bin:$PATH"
TASK=${SLURM_ARRAY_TASK_ID:-0}
(( TASK >= 0 && TASK <= 6 ))
modes=(off basic strong auto)
OUT="${PRIMO_FUZZ_OUT_DIR:?set PRIMO_FUZZ_OUT_DIR}/task-$TASK"
mkdir -p "$OUT"
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?set PRIMO_EXPECTED_GIT}" "$OUT/version.txt"
command -v dolmen
command -v z3
if (( TASK < 4 )); then
  options=(--check-model --preprocessing-mode="${modes[TASK]}")
else
  options=(--check-model --preprocessing-mode="${modes[TASK - 3]}" --monotone-elimination)
fi
solver_args=()
for arg in "${options[@]}"; do solver_args+=(--solver-arg="$arg"); done
printf '%s\n' "${options[@]}" > "$OUT/options.txt"
# version.txt is authoritative: archive workspaces may retain stale .git data.
python3 scripts/qf_lra_boolean_arithmetic_fuzz.py --solver ./build-slurm-rel/primo \
  "${solver_args[@]}" --count "${PRIMO_FUZZ_COUNT:-1000}" --seed "$((202609700 + TASK))" \
  --timeout 5 --dolmen-timeout 5 --out "$OUT/campaign"
