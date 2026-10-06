#!/bin/bash
#SBATCH --job-name=primo-profile-flatten-off
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:08:00
#SBATCH --output=slurm_logs/%x-%j.out
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
OUT=$PWD/results/integer-loss-profile-flatten-off
mkdir "$OUT"
hostname > "$OUT/node.txt"
for arm in baseline candidate; do
 binary=${PRIMO_BASELINE_SOLVER:?}
 if [[ $arm == candidate ]]; then binary=${PRIMO_CANDIDATE_SOLVER:?}; fi
 python3 results/profile_gdb_samples.py --out "$OUT/$arm" --timeout 180 --interval 0.2 --seed 203610602 -- "$binary" --quiet --stats --boolean-flatten-post-threshold=0 "${PRIMO_PROFILE_INPUT:?}"
done
