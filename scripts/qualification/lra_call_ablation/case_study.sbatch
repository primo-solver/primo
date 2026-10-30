#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-case-study
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --hint=nomultithread
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PYTHONPATH="$PWD/scripts"
artifacts="${PRIMO_CASE_ARTIFACTS:?}"
results="${PRIMO_CASE_RESULTS:?}"
corpus="${PRIMO_CASE_CORPUS:?}"
case "${PRIMO_CASE_PHASE:?}" in
 collect)
  python3 "$artifacts/prepare_lra_case_study.py" collect \
   --extraction "$results/extract" \
   --source-manifest "$artifacts/qf-lra-case-study-sources.txt" \
   --include "$artifacts/existing/benchmarks/qf-lra-call-pilot" \
   --include "$artifacts/existing/benchmarks/qf-lra-captured-calls" \
   --out "$results/pool"
  python3 "$artifacts/prepare_lra_case_study.py" select \
   --pool "$results/pool" --out "$corpus" --target 1000 \
   --seed 20261030 --max-per-parent 25 --max-per-family 400
  ;;
 references)
  labels=(yices-smt2 opensmt)
  index=${SLURM_ARRAY_TASK_ID:?}
  label=${labels[$((index / 32))]}
  shard=$((index % 32))
  python3 scripts/benchmark_model_replay.py benchmark \
   --root "$corpus" --manifest "$corpus/manifest.txt" \
   --solver "$PWD/tmp/reference-panel/$label" \
   --timeout 15 --shards 32 --shard-index "$shard" \
   --run-id lra-case-study-1000-20261030 \
   --out "$results/references/$label/shard-$shard"
  ;;
 *) exit 2 ;;
esac
