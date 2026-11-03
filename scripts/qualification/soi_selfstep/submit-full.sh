#!/usr/bin/env bash
# Run only after the frozen-worktree SLURM preparation/preflight and pilot gate.
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
plan_dir=${PRIMO_SOI_PLAN_DIR:-benchmarks/qf-lra-simplex-ablation}
summary_script=${PRIMO_SELFSTEP_SUMMARY_SCRIPT:-scripts/qualification/soi_selfstep/summarize.sbatch}
if [[ $plan_dir != /* ]]; then plan_dir="$PWD/$plan_dir"; fi
test ! -e results/full/plan.json
test ! -e results/profiles/plan.json
export PRIMO_SOI_PLAN="$plan_dir/soi-selfstep-plan.json"
export PRIMO_SOI_CORPUS="$PWD/benchmarks/qf-lra-case-study-1000"
export PRIMO_SOI_SOLVER="$PWD/qualified/candidate/primo"
export PRIMO_SOI_BINARY_MAP="$PWD/qualified/binaries.json"
export PRIMO_SOI_OUT="$PWD/results/full" PRIMO_SOI_SHARDS=1000 PRIMO_SOI_PHASE=timing
export PRIMO_DOLMEN="$PWD/contrib/local/bin/dolmen"
mkdir -p "$PRIMO_SOI_OUT" results/profiles logs
cp "$PRIMO_SOI_PLAN" "$PRIMO_SOI_OUT/plan.json"
cp "$plan_dir/soi-selfstep-profile-plan.json" results/profiles/plan.json
timing=$(sbatch --parsable --partition=compute --array=0-999%128 --time=00:15:00 \
 --output=logs/full-timing-%A_%a.log --export=ALL scripts/qualification/soi_improvements/measure.sbatch)
printf 'timing %s\n' "$timing"
export PRIMO_SOI_PHASE=models
models=$(sbatch --parsable --partition=compute --array=0-999%128 --time=01:00:00 \
 --dependency="afterok:$timing" --output=logs/full-models-%A_%a.log --export=ALL \
 scripts/qualification/soi_improvements/measure.sbatch)
printf 'models %s\n' "$models"
export PRIMO_SOI_PHASE=profile PRIMO_SOI_OUT="$PWD/results/profiles"
export PRIMO_SOI_PLAN="$plan_dir/soi-selfstep-profile-plan.json"
profiles=$(sbatch --parsable --partition=compute --array=0-999%128 --time=00:08:00 \
 --dependency="afterok:$timing" --output=logs/full-profile-%A_%a.log --export=ALL \
 scripts/qualification/soi_improvements/measure.sbatch)
printf 'profiles %s\n' "$profiles"
export PRIMO_TIMING_ROOT="$PWD/results/full" PRIMO_PROFILE_ROOT="$PWD/results/profiles"
export PRIMO_SUMMARY_OUT="$PWD/results/full-summary.json"
export PRIMO_QUALIFICATION_OUT="$PWD/results/full-qualification.json"
sbatch --parsable --partition=compute --dependency="afterok:$timing:$models:$profiles" \
 --output=logs/full-summary-%j.log --export=ALL "$summary_script"
