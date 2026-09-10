#!/usr/bin/env bash
#SBATCH --job-name=primo-assumptions-gate
#SBATCH --array=0-7%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
slot=$SLURM_ARRAY_TASK_ID
kind=generated
seed_args=()
if (( slot >= 4 )); then
  kind=native
  seed_args=(--native-seed "${PRIMO_NATIVE_PRIOR:?}/task-0/model-prefix/original.smt2"
             --native-seed "$PRIMO_NATIVE_PRIOR/task-1/model-prefix/original.smt2")
fi
python3 scripts/incremental_lra_gate.py --kind "$kind" "${seed_args[@]}" \
  --seed "$((202609110 + slot * 250))" --count 250 \
  --solver build-slurm-rel/primo --solver-arg=--quiet \
  --solver-arg=--incremental-mode=assumptions --solver-arg=--max-theory-conflicts=10000 \
  --ref contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
  --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
  --timeout 15 --dolmen-timeout 20 --out "${PRIMO_INCREMENTAL_OUT:?}/shard-$slot"
