#!/usr/bin/env bash
# Prepare a clean archive and run the repository --check-only preflight first.
#SBATCH --job-name=primo-lexical-policy
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err

set -euo pipefail
cd "${PRIMO_WORKDIR:?prepared workspace required}"
OUT=${PRIMO_LEXICAL_OUT:?fresh output directory required}
REV=${PRIMO_EXPECTED_REVISION:?expected source revision required}
export PATH="$PWD/contrib/local/bin:$PATH"
./build-slurm-rel/primo --version | grep -Fx "git: $REV"
python3 scripts/check_smtlib_lexical_conformance.py \
  --primo build-slurm-rel/primo --policy --mutations 1000 --seed 20260910 \
  --source-commit "$REV" --only primo z3 cvc5 --out "$OUT/policy"
python3 scripts/validate_model.py --solver build-slurm-rel/primo \
  --solver-arg=--quiet --solver-arg=--check-model \
  --solver-arg=--max-theory-conflicts=1000 --expect-sat --require-dolmen-pass \
  --out "$OUT/model" benchmarks/smoke/quoted_names_model.smt2
# Reuse the established whole-source static workflow and provisioned compiler headers.
PRIMO_REVIEW_OUT_DIR="$OUT/static" PRIMO_EXPECTED_GIT="$REV" \
  bash scripts/slurm_typed_model_static_review.sbatch
