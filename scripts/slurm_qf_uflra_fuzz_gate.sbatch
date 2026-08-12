#!/usr/bin/env bash
# Reusable SLURM gate for general and finite-domain QF_UFLRA fuzzing.
#
# Submit from a prepared remote workspace, for example:
#   sbatch scripts/slurm_qf_uflra_fuzz_gate.sbatch
#
# Override PRIMO_WORKDIR, PRIMO_FUZZ_COUNT, PRIMO_FUZZ_SEED_BASE,
# PRIMO_FUZZ_SOLVER_ARGS, PRIMO_FUZZ_OUT_DIR, or
# PRIMO_FINITE_DOMAIN_REQUIRE_POST_AMO in the sbatch environment when needed.
# PRIMO_FUZZ_SOLVER_ARGS is split on spaces into repeated --solver-arg values;
# use it only for option tokens that contain no spaces. Set the AMO requirement
# to 0 when testing a selector that intentionally chooses a non-AMO profile.
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
FINITE_COUNT=${PRIMO_FINITE_DOMAIN_FUZZ_COUNT:-125}
SEED_BASE=${PRIMO_FUZZ_SEED_BASE:-202607390}
OUT_ROOT=${PRIMO_FUZZ_OUT_DIR:-slurm-results/qf_uflra-fuzz-gate}
SOLVER_ARGS_TEXT=${PRIMO_FUZZ_SOLVER_ARGS:-}
REQUIRE_POST_AMO=${PRIMO_FINITE_DOMAIN_REQUIRE_POST_AMO:-1}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"

TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
SEED=$((SEED_BASE + TASK_ID))
OUT="$OUT_ROOT/task-${TASK_ID}"

./build-slurm-rel/primo --version
for required in z3 cvc5; do
  if ! PATH="$WORKDIR/contrib/local/bin:$PATH" command -v "$required" >/dev/null 2>&1; then
    echo "required reference '$required' not found; prepare contrib-z3 and contrib-cvc5" >&2
    exit 2
  fi
done
solver_args=()
solver_arg_words=()
finite_domain_activation_args=()
case "$REQUIRE_POST_AMO" in
  0) ;;
  1) finite_domain_activation_args+=(--require-post-amo) ;;
  *)
    echo "PRIMO_FINITE_DOMAIN_REQUIRE_POST_AMO must be 0 or 1" >&2
    exit 2
    ;;
esac
if [[ -n $SOLVER_ARGS_TEXT ]]; then
  read -r -a solver_arg_words <<< "$SOLVER_ARGS_TEXT"
  for arg in "${solver_arg_words[@]}"; do
    solver_args+=(--solver-arg="$arg")
  done
fi
printf 'solver options:'
printf ' %q' "${solver_arg_words[@]}"
printf '\n'
PATH="$WORKDIR/contrib/local/bin:$PATH" \
  python3 scripts/qf_uflra_smoke_fuzz.py \
    --solver ./build-slurm-rel/primo \
    "${solver_args[@]}" \
    --ref z3 \
    --count "$COUNT" \
    --seed "$SEED" \
    --timeout 5 \
    --shape mixed \
    --out "$OUT/general"

PATH="$WORKDIR/contrib/local/bin:$PATH" \
  python3 scripts/finite_domain_fuzz.py \
    --solver ./build-slurm-rel/primo \
    "${solver_args[@]}" \
    --solver-arg=--qf-uf-preprocess=strong \
    --ref z3 \
    --ref cvc5 \
    --logic QF_UF \
    --count "$FINITE_COUNT" \
    --seed "$((SEED + 100000))" \
    --timeout 5 \
    "${finite_domain_activation_args[@]}" \
    --out "$OUT/finite-qf-uf"

PATH="$WORKDIR/contrib/local/bin:$PATH" \
  python3 scripts/finite_domain_fuzz.py \
    --solver ./build-slurm-rel/primo \
    "${solver_args[@]}" \
    --ref z3 \
    --ref cvc5 \
    --logic QF_UFLRA \
    --count "$FINITE_COUNT" \
    --seed "$((SEED + 200000))" \
    --timeout 5 \
    "${finite_domain_activation_args[@]}" \
    --out "$OUT/finite-qf-uflra"
