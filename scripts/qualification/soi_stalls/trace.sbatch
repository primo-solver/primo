#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-stall-trace
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:05:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
out="${PRIMO_TRACE_OUT:?}"
solver="${PRIMO_TRACE_SOLVER:-build-slurm-rel/primo}"
export ASAN_OPTIONS=detect_leaks=0
export DEBUGINFOD_URLS=
if [[ -n ${PRIMO_TRACE_LIBRARY_PATH:-} ]]; then
 export LD_LIBRARY_PATH="$PRIMO_TRACE_LIBRARY_PATH:${LD_LIBRARY_PATH:-}"
fi
mkdir -p "$out"
hostname > "$out/host.txt"
lscpu > "$out/cpu.txt"
sha256sum "$solver" > "$out/binary.sha256"
"$solver" --version > "$out/version.txt"
for name in \
 f9874940bbbfe7ce16d588d10faf6173222a1f9b83eb4e9758619dfd3d269c0a \
 00f92b151e8a8509882ef220886f1e17bc378d9c494ab808d38e1fe537b80631 \
 02b44138c0e44524d4f6f9417725c1de333625ce3b177e26a55f54775f112a7b; do
 export PRIMO_TRACE_JSON="$out/$name.json"
 timeout --signal=TERM --kill-after=5s 60s gdb -q -batch \
  -ex 'set pagination off' -ex 'set confirm off' \
  -ex "source ${PRIMO_TRACE_CALLBACK:?}" -ex run \
  --args "$solver" --quiet --stats --gaussian-elimination \
  --lra-pivoting-rule=soi --lra-soi-selection=short-column \
  --lra-soi-pricing-candidates=64 --lra-soi-stall-limit=8 \
  --lra-soi-cached-conflicts --lra-soi-linear-maintenance \
  --lra-soi-minimize=row-growing --lra-soi-suspend-on-fallback \
  "${PRIMO_CORPUS_ROOT:?}/cubes/$name.smt2" > "$out/$name.log" 2>&1
done
