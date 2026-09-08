#!/usr/bin/env bash
# Full cross-product: four preprocessing modes, model guidance and online mode.
# Four shards per profile: default gets 3000 cases, each other profile 1000.
#SBATCH --job-name=primo-component-fuzz
#SBATCH --array=0-63%8
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
TASK=${SLURM_ARRAY_TASK_ID:?}
PROFILE=$((TASK / 4))
(( PROFILE < 16 )) || exit 2
modes=(auto off basic strong)
options=("--preprocessing-mode=${modes[$((PROFILE % 4))]}")
if (( PROFILE & 4 )); then options+=(--no-qf-uflia-model-combination); fi
if (( PROFILE & 8 )); then options+=(--no-qf-uflia-online-components); fi
export PRIMO_FUZZ_SOLVER_ARGS="${options[*]}"
export PRIMO_FUZZ_COUNT=250
if (( PROFILE == 0 )); then PRIMO_FUZZ_COUNT=750; fi
export PRIMO_FUZZ_SEED_BASE=${PRIMO_CAMPAIGN_SEED:-202609080}
export PRIMO_FUZZ_OUT_DIR="${PRIMO_CAMPAIGN_OUT:?}/profile-$PROFILE"
mkdir -p "$PRIMO_FUZZ_OUT_DIR"
./build-slurm-rel/primo --version | tee "$PRIMO_FUZZ_OUT_DIR/version-$TASK.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$PRIMO_FUZZ_OUT_DIR/version-$TASK.txt"
if (( TASK % 4 == 0 )); then
  for seeds in benchmarks/regressions benchmarks/smoke/qf_uflia; do
    python3 scripts/check_solver_seed_corpus.py \
      --seeds "$seeds" --solver ./build-slurm-rel/primo --solver-arg=--quiet \
      "${options[@]/#/--solver-arg=}" --ref contrib/local/bin/z3 --timeout 10 \
      --out "$PRIMO_FUZZ_OUT_DIR/seeds-${seeds##*/}.csv"
  done
fi
bash scripts/slurm_qf_uflia_model_fuzz_gate.sbatch
