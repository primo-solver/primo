#!/usr/bin/env bash
# Diagnostic Callgrind run, never used as headline timing evidence.
#SBATCH --job-name=primo-incremental-profile
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
# Compute-node network access is not required for instruction profiling.
export DEBUGINFOD_URLS=
cd "${PRIMO_WORKDIR:?}"
out=${PRIMO_PROFILE_OUT:?}
mkdir -p "$out"
for label in baseline candidate; do
  solver=${PRIMO_CANDIDATE:?}
  if [[ "$label" == baseline ]]; then solver=${PRIMO_BASELINE:?}; fi
  "$solver" --version > "$out/$label.version"
  valgrind --tool=callgrind --callgrind-out-file="$out/$label.callgrind" \
    "$solver" --quiet --max-theory-conflicts=10000 "${PRIMO_PROFILE_INPUT:?}" \
    > "$out/$label.stdout" 2> "$out/$label.stderr"
  callgrind_annotate --inclusive=yes --threshold=99 "$out/$label.callgrind" \
    > "$out/$label.inclusive.txt"
done
