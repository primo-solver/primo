#!/usr/bin/env bash
# Run only after the scoped differential gate passes. If static review is still
# running, pin timing to a different node; never share its measurement resources.
# Submit timing and model replay separately; replay depends on all timing tasks.
#SBATCH --job-name=primo-incremental-preserve
#SBATCH --array=0-4%1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err

set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
logics=(QF_UF QF_LIA QF_LRA QF_UFLRA QF_UFLIA)
roots=("${PRIMO_UF_ROOT:?}" "${PRIMO_LIA_ROOT:?}" "${PRIMO_LRA_ROOT:?}"
       "${PRIMO_UFLRA_ROOT:?}" "${PRIMO_UFLIA_ROOT:?}")
task=${SLURM_ARRAY_TASK_ID:?}
logic=${logics[$task]}
root=${roots[$task]}
out="${PRIMO_PRESERVATION_OUT:?}/$logic"
phase=${PRIMO_PHASE:?}
if [[ "$phase" == benchmark ]]; then
  mkdir -p "$out"
  hostname > "$out/node-0.txt"
fi
for round in 0 1; do
  if [[ "$phase" == replay ]]; then
    python3 scripts/benchmark_model_replay.py replay \
      --out "$out/round-$round/candidate/shard-0" \
      --model-timeout 30 --dolmen-timeout 30 --dolmen-size 4G --dolmen-arg=--strict=false
    continue
  fi
  [[ "$phase" == benchmark ]] || exit 2
  order=(baseline candidate)
  if [[ "$round" == 1 ]]; then order=(candidate baseline); fi
  for label in "${order[@]}"; do
    solver=${PRIMO_CANDIDATE:?}
    revision=${PRIMO_CANDIDATE_REVISION:?}
    if [[ "$label" == baseline ]]; then
      solver=${PRIMO_BASELINE:?}
      revision=${PRIMO_BASELINE_REVISION:?}
    fi
    python3 scripts/benchmark_model_replay.py benchmark \
      --root "$root" \
      --manifest "${PRIMO_MANIFEST_ROOT:?}/incremental-preservation-${logic,,}.txt" \
      --solver "$solver" --solver-arg=--quiet --solver-arg=--stats \
      --expected-git "$revision" --timeout 10 \
      --run-id "incremental-preservation-$logic-$round-$label" \
      --out "$out/round-$round/$label/shard-0"
  done
done
