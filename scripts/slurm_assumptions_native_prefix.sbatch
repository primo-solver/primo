#!/usr/bin/env bash
#SBATCH --job-name=primo-assumptions-prefix
#SBATCH --array=0-9%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 scripts/check_assumptions_native_prefix.py \
  --manifest benchmarks/native-incremental-qf-lra.json --root "${PRIMO_NATIVE_ROOT:?}" \
  --prior "${PRIMO_NATIVE_PRIOR:?}" --index "$SLURM_ARRAY_TASK_ID" \
  --solver build-slurm-rel/primo --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
  --solver-arg=--quiet --solver-arg=--incremental-mode=assumptions \
  --solver-arg=--max-theory-conflicts=10000 --dolmen contrib/local/bin/dolmen \
  --out "${PRIMO_NATIVE_OUT:?}/task-$SLURM_ARRAY_TASK_ID"
