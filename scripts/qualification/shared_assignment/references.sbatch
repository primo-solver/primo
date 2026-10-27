#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-refs
#SBATCH --array=0-3%4
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
for reference in z3 cvc5; do
 python3 scripts/benchmark_model_replay.py benchmark \
  --root tmp/shared-assignment-data/comparison --manifest tmp/default-inputs.txt \
  --solver "contrib/local/bin/$reference" --timeout 5 --shards 4 --shard-index "$SLURM_ARRAY_TASK_ID" \
  --run-id "shared-reference-$reference-$SLURM_ARRAY_JOB_ID-$SLURM_ARRAY_TASK_ID" \
  --out "${PRIMO_REFERENCE_OUT:?}/$reference/shard-$SLURM_ARRAY_TASK_ID"
done
