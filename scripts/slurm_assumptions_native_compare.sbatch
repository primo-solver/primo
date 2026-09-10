#!/usr/bin/env bash
#SBATCH --job-name=primo-assumptions-native
#SBATCH --array=0-3%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:12:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
index=$((SLURM_ARRAY_TASK_ID / 2))
mode=rebuild
if (( SLURM_ARRAY_TASK_ID % 2 )); then mode=assumptions; fi
python3 scripts/compare_native_incremental.py \
  --manifest benchmarks/native-incremental-qf-lra.json --root "${PRIMO_NATIVE_ROOT:?}" \
  --prior-out "${PRIMO_NATIVE_PRIOR:?}" --index "$index" --solver build-slurm-rel/primo \
  --label primo --solver-arg=--quiet --solver-arg=--stats \
  --solver-arg="--incremental-mode=$mode" --solver-arg=--max-theory-conflicts=10000 \
  --expected-git "${PRIMO_EXPECTED_REVISION:?}" --timeout 600 \
  --out "${PRIMO_NATIVE_OUT:?}/case-$index-$mode"
