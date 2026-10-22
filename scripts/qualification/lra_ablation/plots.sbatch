#!/usr/bin/env bash
# Draw all four native profiles only after complete separate model audits.
#SBATCH --job-name=primo-lra-ablation-plots
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%A.out
#SBATCH --error=logs/%x-%A.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
for dataset in full30 selection1200; do
  timeout=30
  [[ $dataset != selection1200 ]] || timeout=1200
  root="${PRIMO_CAMPAIGN_OUT:?}/$dataset"
  [[ -s $root/qualified-audit.json ]] || { echo "Missing qualified audit: $root" >&2; exit 2; }
  python3 scripts/plot_cactus.py \
    --series "default=$root/plot-data" --series "phase=$root/plot-data" \
    --series "monotone=$root/plot-data" --series "both=$root/plot-data" \
    --timeout "$timeout" --out "$root/cactus.svg" \
    --title "QF_LRA migration-branch ablation: $dataset" \
    --note 'Primo 4200525; same binary; compute Xeon E5-2690 v4; 1 physical CPU, 7 GiB per measurement. Separate direct Dolmen SAT-model checks. Default promotion pending reference joins and repeated-loss triage.'
done
