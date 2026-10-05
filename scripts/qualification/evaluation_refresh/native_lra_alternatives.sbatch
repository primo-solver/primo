#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-alt-references
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=02:00:00
#SBATCH --hint=nomultithread
#SBATCH --array=0-3%4
#SBATCH --output=results/lra-alt-references-%A_%a.out
#SBATCH --error=results/lra-alt-references-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
indices=(6 8)
index=${indices[$((SLURM_ARRAY_TASK_ID % 2))]}
reference=yices-smt2
if (( SLURM_ARRAY_TASK_ID >= 2 )); then reference=opensmt; fi
python3 scripts/native_uf_sessions.py validate \
  --root "${PRIMO_LRA_ROOT:?}" --manifest results/native-lra-manifest.json \
  --shards 16 --shard-index "$index" --solver build-slurm-rel/primo \
  --solver-arg=--quiet --solver-arg=--stats --solver-arg=--max-theory-conflicts=10000 \
  --solver-arg=--incremental-mode=assumptions --expected-git 5efbd02 \
  --timing-out "results/refresh-native-lra/timing/assumptions/shard-$index" \
  --ref "contrib/local/bin/$reference" --dolmen contrib/local/bin/dolmen \
  --timeout 600 --dolmen-timeout 60 \
  --out "results/refresh-native-lra/alternative-references/$reference/assumptions/shard-$index"
