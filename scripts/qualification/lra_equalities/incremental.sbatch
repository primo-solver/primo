#!/usr/bin/env bash
# Fresh push/pop/model checks for the supported equality-only retained profile.
# Monotone elimination is intentionally forbidden in assumptions mode.
#SBATCH --job-name=primo-lra-equality-sessions
#SBATCH --array=0,2,4,6%4
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=7G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
slot=${SLURM_ARRAY_TASK_ID:?}
flags=(--solver-arg=--quiet --solver-arg=--lra-equality-propagation)
if (( slot % 2 )); then
  echo 'Use even slots: monotone-elimination portfolio arms require rebuild mode.' >&2
  exit 2
fi
exec python3 scripts/incremental_lra_gate.py --kind generated \
  --seed "$((2026102150 + (slot / 2) * 250))" --count 250 \
  --solver build-slurm-rel/primo "${flags[@]}" \
  --solver-arg=--incremental-mode=assumptions --solver-arg=--max-theory-conflicts=10000 \
  --ref contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
  --expected-git "${PRIMO_EXPECTED_GIT:?}" --timeout 15 --dolmen-timeout 20 \
  --out "${PRIMO_INCREMENTAL_OUT:?}/shard-$slot"
