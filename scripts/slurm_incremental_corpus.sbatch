#!/usr/bin/env bash
# Standalone correctness only: no reference reruns or headline speed claims.
#SBATCH --job-name=primo-incremental-corpus
#SBATCH --array=0-4%2
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
logics=(QF_UF QF_LIA QF_LRA QF_UFLRA QF_UFLIA)
roots=("${PRIMO_UF_ROOT:?}" "${PRIMO_LIA_ROOT:?}" "${PRIMO_LRA_ROOT:?}"
       "${PRIMO_UFLRA_ROOT:?}" "${PRIMO_UFLIA_ROOT:?}")
task=${SLURM_ARRAY_TASK_ID:?}
python3 scripts/check_incremental_corpus.py \
  --manifest benchmarks/incremental-parser-real-25.json \
  --root "${roots[$task]}" --logic "${logics[$task]}" \
  --solver build-slurm-rel/primo --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
  --chunk-bytes 127 --timeout 30 --dolmen-timeout 30 \
  --out "${PRIMO_CORPUS_OUT:?}/${logics[$task]}"
