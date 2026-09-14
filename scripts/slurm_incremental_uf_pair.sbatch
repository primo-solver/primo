#!/usr/bin/env bash
#SBATCH --job-name=primo-uf-atom-scan-pair
#SBATCH --array=0-4%2
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
indices=(14 267 852 1688 656)
index=${indices[${SLURM_ARRAY_TASK_ID:?}]}
phase=${PRIMO_UF_PHASE:-timing}
if [[ $phase == validate ]]; then
  python3 scripts/native_uf_sessions.py validate \
    --root "${PRIMO_UF_ROOT:?}" --manifest "${PRIMO_UF_MANIFEST:?}" \
    --shards 1778 --shard-index "$index" \
    --solver build-slurm-rel/primo --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
    --solver-arg=--quiet --solver-arg=--stats \
    --solver-arg=--incremental-mode=assumptions --solver-arg=--max-theory-conflicts=10000 \
    --timing-out "${PRIMO_UF_TIMING:?}/candidate/round-0/index-$index" \
    --ref contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
    --timeout 120 --dolmen-timeout 120 --out "${PRIMO_INCREMENTAL_OUT:?}/index-$index"
  exit 0
fi
if [[ $phase != timing ]]; then exit 2; fi
for repetition in 0 1 2; do
  labels=(baseline candidate)
  if (( repetition % 2 )); then labels=(candidate baseline); fi
  for label in "${labels[@]}"; do
    solver=build-slurm-rel/primo
    revision=${PRIMO_EXPECTED_REVISION:?}
    if [[ $label == baseline ]]; then
      solver=${PRIMO_UF_BASELINE:?}
      revision=${PRIMO_UF_BASELINE_REVISION:?}
    fi
    python3 scripts/native_uf_sessions.py timing \
      --root "${PRIMO_UF_ROOT:?}" --manifest "${PRIMO_UF_MANIFEST:?}" \
      --shards 1778 --shard-index "$index" --solver "$solver" --expected-git "$revision" \
      --solver-arg=--quiet --solver-arg=--stats \
      --solver-arg=--incremental-mode=assumptions --solver-arg=--max-theory-conflicts=10000 \
      --timeout 30 --out "${PRIMO_INCREMENTAL_OUT:?}/$label/round-$repetition/index-$index"
  done
done
