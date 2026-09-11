#!/usr/bin/env bash
#SBATCH --job-name=primo-native-uf
#SBATCH --array=0-63%8
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=02:00:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
slot=$SLURM_ARRAY_TASK_ID
# Interleave profiles so reference and candidate results arrive together.
profile=$((slot % 4))
shard=$((slot / 4))
label=rebuild
solver=build-slurm-rel/primo
solver_args=(--solver-arg=--quiet --solver-arg=--stats --solver-arg=--max-theory-conflicts=10000)
revision_args=(--expected-git "${PRIMO_EXPECTED_REVISION:?}")
case $profile in
  0) solver_args+=(--solver-arg=--incremental-mode=rebuild) ;;
  1) label=assumptions; solver_args+=(--solver-arg=--incremental-mode=assumptions) ;;
  2) label=z3; solver=contrib/local/bin/z3; solver_args=(--solver-arg=-in); revision_args=() ;;
  3) label=cvc5; solver=${PRIMO_CVC5:?}; solver_args=(--solver-arg=--incremental --solver-arg=--lang=smt2); revision_args=() ;;
esac
mode=${PRIMO_UF_PHASE:-timing}
validation_args=()
if [[ $mode == validate ]]; then
  if (( profile >= 2 )); then exit 0; fi
  validation_args=(--timing-out "${PRIMO_UF_TIMING:?}/$label/shard-$shard"
    --ref contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen)
fi
python3 scripts/native_uf_sessions.py "$mode" \
  --root "${PRIMO_UF_ROOT:?}" --manifest "${PRIMO_UF_MANIFEST:?}" \
  --shards 16 --shard-index "$shard" --solver "$solver" "${solver_args[@]}" \
  "${revision_args[@]}" "${validation_args[@]}" \
  --timeout 30 --dolmen-timeout 60 --out "${PRIMO_INCREMENTAL_OUT:?}/$label/shard-$shard"
