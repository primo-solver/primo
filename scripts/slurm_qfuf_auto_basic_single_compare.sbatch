#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuf-basic-single
#SBATCH --array=0-31
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=03:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

export PRIMO_OPTION_MANIFESTS=${PRIMO_OPTION_MANIFESTS:-"benchmarks/user-primo-html334-2026-07-18.txt"}
export PRIMO_OPTION_VARIANTS=${PRIMO_OPTION_VARIANTS:-"default= no-basic-single=--no-auto-basic-single"}
export PRIMO_OPTION_SHARDS=${PRIMO_OPTION_SHARDS:-16}
export PRIMO_OPTION_TIMEOUT=${PRIMO_OPTION_TIMEOUT:-120}
export PRIMO_OPTION_OUT_ROOT=${PRIMO_OPTION_OUT_ROOT:-"${PRIMO_WORKDIR:-/home/janotmik/primo-cadical-profile}/results/qfuf-auto-basic-single-${SLURM_ARRAY_JOB_ID:-manual}"}

bash scripts/slurm_qfuf_option_compare.sbatch
