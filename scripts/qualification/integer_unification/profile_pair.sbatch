#!/usr/bin/env bash
# Diagnostic GDB samples of frozen binaries; never interpret as paired timings.
#SBATCH --job-name=primo-integer-stack-pair
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
OUT=${PRIMO_PROFILE_OUT:?}
SAMPLER=${PRIMO_PROFILE_SAMPLER:-scripts/profile_gdb_samples.py}
mkdir "$OUT"
hostname > "$OUT/node.txt"
lscpu > "$OUT/cpu.txt"
gdb --version > "$OUT/gdb-version.txt"
sha256sum "${PRIMO_BASELINE_SOLVER:?}" "${PRIMO_CANDIDATE_SOLVER:?}" \
  "${PRIMO_PROFILE_INPUT:?}" "$SAMPLER" > "$OUT/identities.sha256"
for round in 0 1; do
  arms=(baseline candidate)
  if (( round % 2 )); then arms=(candidate baseline); fi
  for arm in "${arms[@]}"; do
    binary=$PRIMO_BASELINE_SOLVER
    if [[ $arm == candidate ]]; then binary=$PRIMO_CANDIDATE_SOLVER; fi
    python3 "$SAMPLER" --out "$OUT/round-$round-$arm" \
      --timeout 180 --interval 0.2 --seed "$((203610600 + round))" \
      -- "$binary" --quiet --stats "$PRIMO_PROFILE_INPUT"
  done
done
sha256sum --check "$OUT/identities.sha256"
