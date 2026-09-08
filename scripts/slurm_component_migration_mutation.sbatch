#!/usr/bin/env bash
# Same 16-profile matrix, family-complete seeds, operand-preserving OpFuzz.
#SBATCH --job-name=primo-component-mutation
#SBATCH --array=0-15%4
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
PROFILE=${SLURM_ARRAY_TASK_ID:?}
(( PROFILE < 16 )) || exit 2
modes=(auto off basic strong)
options=("--preprocessing-mode=${modes[$((PROFILE % 4))]}")
if (( PROFILE & 4 )); then options+=(--no-qf-uflia-model-combination); fi
if (( PROFILE & 8 )); then options+=(--no-qf-uflia-online-components); fi
export PRIMO_FUZZ_OUT_ROOT="${PRIMO_CAMPAIGN_OUT:?}/profile-$PROFILE"
export PRIMO_MODEL_SEEDS="$PRIMO_FUZZ_OUT_ROOT/seeds"
export PRIMO_FUZZ_OUT_DIR="$PRIMO_FUZZ_OUT_ROOT"
export PRIMO_MUTATION_SIZE_LIMIT=2000000
export PRIMO_MUTATION_SECONDS=${PRIMO_MUTATION_SECONDS:-600}
export PRIMO_MUTATION_ITERATIONS=${PRIMO_MUTATION_ITERATIONS:-100}
export PRIMO_MUTATION_TIMEOUT=${PRIMO_MUTATION_TIMEOUT:-5}
export PRIMO_MUTATION_SEED_TIMEOUT=${PRIMO_MUTATION_SEED_TIMEOUT:-60}
export PRIMO_MUTATION_COLLECT_RESOURCES=1
export PRIMO_MUTATION_TOOL=opfuzz
export PRIMO_EXTRA_SOLVER_ARGS="${options[*]:1}"
mkdir -p "$PRIMO_FUZZ_OUT_ROOT"
./build-slurm-rel/primo --version | tee "$PRIMO_FUZZ_OUT_ROOT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$PRIMO_FUZZ_OUT_ROOT/version.txt"
python3 scripts/build_fuzz_seed_corpus.py --root "${PRIMO_CORPUS_ROOT:?}" \
  --out "$PRIMO_MODEL_SEEDS" --per-family 2 --max-bytes 200000 \
  | tee "$PRIMO_FUZZ_OUT_ROOT/seed-families.txt"
# A too-large family must be triaged, not silently skipped by the mutator.
python3 - <<'PY'
import os
from pathlib import Path
root = Path(os.environ["PRIMO_MODEL_SEEDS"])
files = sorted(root.glob("*.smt2"))
assert files, "no real mutation seeds"
assert all(path.stat().st_size <= 2000000 for path in files), "seed exceeds mutator budget"
PY
# The existing typed-model gate checks original seeds and every retained mutant
# with a status-aware reference comparison AND independent printed-model check.
# Its mode order is off/basic/strong/auto; our profile order starts with auto.
SLURM_ARRAY_TASK_ID=$(((PROFILE + 3) % 4)) bash scripts/slurm_typed_model_mutation_gate.sbatch
