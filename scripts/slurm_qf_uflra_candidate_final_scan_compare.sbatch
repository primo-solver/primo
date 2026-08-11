#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuflra-cand
#SBATCH --array=0-2
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
export PRIMO_WORKDIR="$WORKDIR"
export PRIMO_COMPARE_ROOT=${PRIMO_COMPARE_ROOT:-tmp/QF_UFLRA}
export PRIMO_COMPARE_MANIFEST=${PRIMO_COMPARE_MANIFEST:-benchmarks/qf_uflra-preprocessing-opportunity-sample.txt}
export PRIMO_COMPARE_TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-10}
export PRIMO_COMPARE_OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/qf-uflra-candidate-final-${SLURM_ARRAY_JOB_ID:-manual}"}
export PRIMO_COMPARE_SOLVERS=${PRIMO_COMPARE_SOLVERS:-"default=build-slurm-rel/primo --quiet --stats;candidate=build-slurm-rel/primo --quiet --stats --qf-uflra-candidate-final-scan;z3=contrib/local/bin/z3"}

exec bash scripts/slurm_qf_uflra_sample_compare.sbatch
