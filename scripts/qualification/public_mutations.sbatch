#!/usr/bin/env bash
#SBATCH --job-name=primo-public-mutations
#SBATCH --array=0-1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
if (( slot == 0 )); then corpus=benchmarks/seeds/qf_lra_real; else corpus=benchmarks/seeds/qf_uflra_scale; fi
out="results/public-mutations/${SLURM_ARRAY_JOB_ID}/shard-$slot"
python3 scripts/quantifier_preparation_gate.py --mode mutations \
  --corpus "$corpus" --solver build-slurm-rel/primo \
  --expected-git "${PRIMO_EXPECTED_REVISION:?}" --ref z3 \
  --count 100 --seed "$((202610020 + slot))" --timeout 5 --out "$out"
python3 scripts/validate_model.py --solver build-slurm-rel/primo \
  --solver-arg=--quiet --dolmen dolmen --require-dolmen-pass \
  --dolmen-arg=--strict=false --timeout 10 --dolmen-timeout 10 \
  --out "$out/models" "$out"/case-*/input.smt2
