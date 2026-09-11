#!/usr/bin/env bash
#SBATCH --job-name=primo-native-uf-retry
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=02:00:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
slot=$SLURM_ARRAY_TASK_ID
profile=$((slot % 4))
shard=$((slot / 4))
label=rebuild
if (( profile == 1 )); then label=assumptions; fi
if (( profile >= 2 )); then echo 'candidate profiles only' >&2; exit 2; fi
# Preserve the failed attempt. Replay the full affected shard with the same
# binary/options and a larger correctness-only budget; never replace timings.
python3 scripts/native_uf_sessions.py validate \
  --root "${PRIMO_UF_ROOT:?}" --manifest "${PRIMO_UF_MANIFEST:?}" \
  --shards 16 --shard-index "$shard" --solver build-slurm-rel/primo \
  --solver-arg=--quiet --solver-arg=--stats --solver-arg=--max-theory-conflicts=10000 \
  --solver-arg="--incremental-mode=$label" --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
  --timing-out "${PRIMO_UF_TIMING:?}/$label/shard-$shard" \
  --ref contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
  --timeout "${PRIMO_UF_VALIDATION_TIMEOUT:-120}" --dolmen-timeout 120 \
  --out "${PRIMO_INCREMENTAL_OUT:?}/$label/shard-$shard"
