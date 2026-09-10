#!/usr/bin/env bash
# Three separate verdicts: full parsing, bounded whole-session execution,
# and independent model checks on an exact native prefix.
#SBATCH --job-name=primo-native-lra-parser
#SBATCH --array=0-9%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 scripts/check_native_incremental.py \
  --manifest benchmarks/native-incremental-qf-lra.json --root "${PRIMO_NATIVE_ROOT:?}" \
  --index "$SLURM_ARRAY_TASK_ID" --solver build-slurm-rel/primo \
  --parser-probe build-slurm-rel/primo_parser_corpus \
  --expected-git "${PRIMO_EXPECTED_REVISION:?}" \
  --parse-timeout 120 --session-timeout 120 --timeout 30 --dolmen-timeout 30 \
  --prefix-queries 4 --out "${PRIMO_NATIVE_OUT:?}/task-$SLURM_ARRAY_TASK_ID"
