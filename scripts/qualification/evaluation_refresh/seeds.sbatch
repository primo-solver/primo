#!/usr/bin/env bash
# Deterministic ground regression seeds precede the timing campaign.
#SBATCH --job-name=primo-refresh-seeds
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_REVISION:?}"
out="results/refresh-seeds-${SLURM_JOB_ID}"
mkdir "$out"
for seeds in qf_uf qf_lra_real qf_uflra qf_uflra_scale; do
  python3 scripts/check_solver_seed_corpus.py --seeds "benchmarks/seeds/$seeds" \
    --solver build-slurm-rel/primo --solver-arg=--quiet --ref contrib/local/bin/z3 \
    --timeout 30 --out "$out/$seeds.csv"
done
