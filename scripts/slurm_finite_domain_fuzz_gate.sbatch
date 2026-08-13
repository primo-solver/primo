#!/usr/bin/env bash
# Focused finite-domain preprocessing gate. The standard QF_UFLRA gate also
# runs finite-domain cases; this matrix isolates preprocessing profiles.
#SBATCH --job-name=primo-finite-domain-fuzz
#SBATCH --output=slurm_logs/primo-finite-domain-fuzz-%A_%a.out
#SBATCH --error=slurm_logs/primo-finite-domain-fuzz-%A_%a.err
#SBATCH --array=0-4
#SBATCH --time=00:20:00
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
COUNT=${PRIMO_FINITE_DOMAIN_FUZZ_COUNT:-200}
SEED_BASE=${PRIMO_FINITE_DOMAIN_FUZZ_SEED_BASE:-2026080800}
OUT_ROOT=${PRIMO_FUZZ_OUT_DIR:-slurm-results/finite-domain-fuzz-gate}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

cd "$WORKDIR"
mkdir -p slurm_logs "$OUT_ROOT"
PATH="$WORKDIR/contrib/local/bin:$PATH"
export PATH

for required in z3 cvc5; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required reference '$required' not found; prepare contrib-z3 and contrib-cvc5" >&2
    exit 2
  fi
done
./build-slurm-rel/primo --version

case "$TASK_ID" in
  0) label=qf-uf-strong; logic=QF_UF; options=(--preprocessing-mode=strong); require_post=1 ;;
  1) label=qf-uf-raw-only; logic=QF_UF; options=(--preprocessing-mode=strong --no-finite-domain-post-amo) ;;
  2) label=qf-uflra-default; logic=QF_UFLRA; options=(); require_post=1 ;;
  3) label=qf-uflra-strong; logic=QF_UFLRA; options=(--preprocessing-mode=strong); require_post=1 ;;
  4) label=qf-uflra-raw-only; logic=QF_UFLRA; options=(--preprocessing-mode=strong --no-finite-domain-post-amo) ;;
  *) echo "task $TASK_ID is outside the active profile matrix" >&2; exit 2 ;;
esac

solver_args=()
post_args=()
for option in "${options[@]}"; do
  solver_args+=(--solver-arg="$option")
done
if [[ ${require_post:-0} == 1 ]]; then
  post_args+=(--require-post-amo)
fi
python3 scripts/finite_domain_fuzz.py \
  --solver ./build-slurm-rel/primo \
  "${solver_args[@]}" \
  --ref z3 \
  --ref cvc5 \
  --logic "$logic" \
  --count "$COUNT" \
  --seed "$((SEED_BASE + TASK_ID))" \
  --timeout 5 \
  "${post_args[@]}" \
  --out "$OUT_ROOT/$label"
