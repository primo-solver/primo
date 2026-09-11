#!/usr/bin/env bash
#SBATCH --job-name=primo-uf-assumptions-gate
#SBATCH --array=0-7%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=01:00:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
slot=$SLURM_ARRAY_TASK_ID
kind=generated
seed_args=()
if (( slot >= 4 )); then
  kind=native
  while IFS= read -r name; do
    seed_args+=(--native-seed "${PRIMO_UF_ROOT:?}/$name")
  done < "${PRIMO_UF_SEEDS:?}"
fi
preprocessing=auto
propagation=online
if (( slot % 2 )); then preprocessing=off; fi
if (( slot % 4 >= 2 )); then propagation=conflict; fi
python3 scripts/incremental_uf_gate.py --kind "$kind" "${seed_args[@]}" \
  --seed "$((202609120 + slot * 250))" --count 250 \
  --solver build-slurm-rel/primo --solver-arg=--quiet \
  --solver-arg=--incremental-mode=assumptions --solver-arg=--max-theory-conflicts=10000 \
  --solver-arg="--preprocessing-mode=$preprocessing" \
  --solver-arg="--euf-propagation=$propagation" \
  --ref contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
  --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
  --timeout 20 --dolmen-timeout 30 --out "${PRIMO_INCREMENTAL_OUT:?}/shard-$slot"
