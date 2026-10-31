#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-simplex
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=04:00:00
#SBATCH --hint=nomultithread
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
artifacts="${PRIMO_SIMPLEX_ARTIFACTS:?}"
export PYTHONPATH="$artifacts/scripts:$PWD/scripts"
if [[ ${PRIMO_SIMPLEX_PHASE:?} == compat ]]; then
 python3 -W error::ResourceWarning -m unittest discover \
  -s "$artifacts/tests/python" -p test_lra_simplex_ablation.py
 exit
fi
extra=()
if [[ ${PRIMO_SIMPLEX_PHASE:?} == models ]]; then
 extra+=(--dolmen "$PWD/contrib/local/bin/dolmen")
fi
python3 "$artifacts/scripts/run_lra_simplex_ablation.py" "$PRIMO_SIMPLEX_PHASE" \
 --plan "$artifacts/benchmarks/qf-lra-simplex-ablation/plan.json" \
 --corpus "${PRIMO_SIMPLEX_CORPUS:?}" --solver "$PWD/build-slurm-rel/primo" \
 --out "${PRIMO_SIMPLEX_OUT:?}" --shards 64 --shard "${SLURM_ARRAY_TASK_ID:?}" \
 "${extra[@]}"
