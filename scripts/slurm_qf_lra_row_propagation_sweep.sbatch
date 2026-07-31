#!/usr/bin/env bash
#SBATCH --job-name=primo-qflra-rowprop-sweep
#SBATCH --array=0-99
#SBATCH --cpus-per-task=1
#SBATCH --mem=12G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Direction D1's cap sweep (docs/qf-lra-heuristics.md section 6.5): does
# row-based bound propagation, run with caps large enough to actually fire,
# move the 500KB-5MB gap band?
#
# Primo only -- z3 and cvc5 are unchanged here, so their numbers come from
# the existing job-117487 CSVs rather than being re-run.
#
# Why these parameters:
#
#  * The manifest is the 59 mid-band files z3 solves at 60s and Primo does
#    not, so at a 60s budget Primo's baseline solves 0 of 59 by
#    construction and every configuration would be compared against zero.
#    The timeout is therefore 300s, where section 6.1a's capability run
#    (45/59 at 600s, median 167s) says a substantial majority already
#    solves -- giving per-file *times* to compare rather than one
#    all-or-nothing bit at the exact boundary where the local n=2 evidence
#    already sits.
#  * The four cap points are the defaults (64/64 forward, 32/32 reverse),
#    then 128, 256 and 1024 applied to all four. 1024 is effectively
#    unlimited on this band -- section 6.2 measured `occurs_in` peaking at
#    521 and rows at 91 terms -- so it tests the local finding that
#    unlimited is *worse* than 256, i.e. that the caps do real work and are
#    merely set an order of magnitude too low.
#  * Both propagation directions are on together for every non-baseline
#    point. The forward/reverse split is not what this sweep is measuring,
#    and separating it would double the job for a second question.
#
# The `--opt=value` form is deliberate: the inner script word-splits each
# solver command on spaces, so an option and its value must be one word.
#
# Validate before submitting:
#   python3 scripts/slurm_validate_sbatch.py \
#     scripts/slurm_qf_lra_row_propagation_sweep.sbatch
#
# Submit:
#   sbatch scripts/slurm_qf_lra_row_propagation_sweep.sbatch
#
# 5 configurations x 20 shards = 100 array tasks; keep --array in step with
# both if either changes, since SBATCH directives cannot read env vars.

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$HOME/primo-cadical-profile}
PRIMO=${PRIMO_SWEEP_BINARY:-build-slurm-rel/primo}
ROW_PROP="--lra-row-propagation --lra-bidirectional-row-propagation"

caps() {
  local n=$1
  printf -- '--lra-row-propagation-max-row-size=%s ' "$n"
  printf -- '--lra-row-propagation-max-fanout=%s ' "$n"
  printf -- '--lra-bidirectional-row-propagation-max-row-size=%s ' "$n"
  printf -- '--lra-bidirectional-row-propagation-max-fanout=%s' "$n"
}

export PRIMO_COMPARE_MANIFEST=${PRIMO_COMPARE_MANIFEST:-benchmarks/qf-lra-scrambled-midband-gap.txt}
export PRIMO_COMPARE_TIMEOUT=${PRIMO_COMPARE_TIMEOUT:-300}
export PRIMO_COMPARE_SHARDS=${PRIMO_COMPARE_SHARDS:-20}
export PRIMO_COMPARE_OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:-"$WORKDIR/results/qf-lra-rowprop-sweep-${SLURM_ARRAY_JOB_ID:-manual}"}
export PRIMO_COMPARE_SOLVERS=${PRIMO_COMPARE_SOLVERS:-"\
base=$PRIMO --quiet;\
rp-default=$PRIMO --quiet $ROW_PROP;\
rp-128=$PRIMO --quiet $ROW_PROP $(caps 128);\
rp-256=$PRIMO --quiet $ROW_PROP $(caps 256);\
rp-1024=$PRIMO --quiet $ROW_PROP $(caps 1024)"}

# Reuses slurm_qf_lra_scrambled_compare.sbatch's body rather than copying
# it: that script is already parameterized by exactly these variables, and
# its own #SBATCH directives are inert comments when run as a plain script.
exec bash "$WORKDIR/scripts/slurm_qf_lra_scrambled_compare.sbatch"
