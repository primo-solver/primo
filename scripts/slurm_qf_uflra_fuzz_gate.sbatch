#!/usr/bin/env bash
# Reusable SLURM gate for the mixed QF_UFLRA smoke fuzzer.
#
# Submit from a prepared remote workspace, for example:
#   sbatch scripts/slurm_qf_uflra_fuzz_gate.sbatch
#
# Override PRIMO_WORKDIR, PRIMO_FUZZ_COUNT, PRIMO_FUZZ_SEED_BASE, or
# PRIMO_FUZZ_OUT_DIR in the sbatch environment when needed.
# Keep cluster hostnames, usernames, and absolute home paths out of this file.
#SBATCH --job-name=primo-qf-uflra-fuzz
#SBATCH --output=slurm_logs/primo-qf-uflra-fuzz-%A_%a.out
#SBATCH --error=slurm_logs/primo-qf-uflra-fuzz-%A_%a.err
#SBATCH --array=0-3
#SBATCH --time=00:20:00
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
COUNT=${PRIMO_FUZZ_COUNT:-750}
SEED_BASE=${PRIMO_FUZZ_SEED_BASE:-202607390}
OUT_ROOT=${PRIMO_FUZZ_OUT_DIR:-slurm-results/qf_uflra-fuzz-gate}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"

TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
SEED=$((SEED_BASE + TASK_ID))
OUT="$OUT_ROOT/task-${TASK_ID}"

./build-slurm-rel/primo --version
PATH="$WORKDIR/contrib/local/bin:$PATH" \
  python3 scripts/qf_uflra_smoke_fuzz.py \
    --solver ./build-slurm-rel/primo \
    --ref z3 \
    --count "$COUNT" \
    --seed "$SEED" \
    --timeout 5 \
    --shape mixed \
    --out "$OUT"
