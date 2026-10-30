#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-case-study-label-model
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --hint=nomultithread
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
results="${PRIMO_CASE_RESULTS:?}"
mkdir -p "$results/label-models"
python3 scripts/validate_model.py --solver build-slurm-rel/primo \
 --manifest "${PRIMO_CASE_LABEL_MANIFEST:?}" --out "$results/label-models/shard-0" \
 --expect-sat --require-dolmen-pass --dolmen contrib/local/bin/dolmen \
 --dolmen-size 2G --dolmen-arg=--strict=false --timeout 60 --dolmen-timeout 30 \
 --solver-arg=--quiet --solver-arg=--stats --solver-arg=--check-model \
 --solver-arg=--lra-call-profile --solver-arg="$results/label-models/replay-profile.jsonl"
