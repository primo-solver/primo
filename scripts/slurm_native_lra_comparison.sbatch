#!/usr/bin/env bash
# Two native sessions x three solvers. Timing diagnostic, not model qualification.
#SBATCH --job-name=primo-native-lra-comparison
#SBATCH --array=0-5%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:12:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
index=$((SLURM_ARRAY_TASK_ID / 3))
slot=$((SLURM_ARRAY_TASK_ID % 3))
case "$slot" in
  0) label=primo; solver=build-slurm-rel/primo
     opts=(--solver-arg=--quiet --solver-arg=--max-theory-conflicts=10000) ;;
  1) label=z3; solver=contrib/local/bin/z3; opts=(--solver-arg=-in --solver-arg=-smt2) ;;
  2) label=cvc5; solver=contrib/local/bin/cvc5
     opts=(--solver-arg=--incremental --solver-arg=--lang=smt2) ;;
esac
python3 scripts/compare_native_incremental.py \
  --manifest benchmarks/native-incremental-qf-lra.json --root "${PRIMO_NATIVE_ROOT:?}" \
  --prior-out "${PRIMO_NATIVE_PRIOR:?}" --index "$index" --solver "$solver" \
  --label "$label" "${opts[@]}" --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
  --timeout 600 --out "${PRIMO_NATIVE_OUT:?}/case-$index-$label"
