#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-selfstep-focused
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:05:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
out="${PRIMO_FOCUSED_OUT:?}"
mkdir -p "$out"
for mode in off on; do
 for selection in paper short-column; do
  options=(--solver-arg=--quiet --solver-arg=--check-model
   --solver-arg=--lra-pivoting-rule=soi
   --solver-arg="--lra-soi-selection=$selection")
  if [[ $mode == off ]]; then options+=(--solver-arg=--preprocessing-mode=off); fi
  if [[ $selection == short-column ]]; then
   options+=(--solver-arg=--lra-soi-pricing-candidates=64
    --solver-arg=--lra-soi-stall-limit=8 --solver-arg=--lra-soi-cached-conflicts
    --solver-arg=--lra-soi-linear-maintenance --solver-arg=--lra-soi-minimize=row-growing)
  fi
  python3 scripts/check_solver_seed_corpus.py --seeds benchmarks/regressions/soi \
   --solver qualified/candidate/primo "${options[@]}" --ref contrib/local/bin/z3 \
   --timeout 10 --out "$out/$selection-$mode.csv"
 done
done
