#!/usr/bin/env bash
#SBATCH --job-name=primo-native-uf-inventory
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:20:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 scripts/native_uf_sessions.py inventory --archive "${PRIMO_UF_ARCHIVE:?}" \
  --root "${PRIMO_UF_ROOT:?}" --manifest "${PRIMO_UF_MANIFEST:?}"
python3 scripts/native_uf_sessions.py parser --root "$PRIMO_UF_ROOT" \
  --manifest "$PRIMO_UF_MANIFEST" --solver build-slurm-rel/primo \
  --expected-git "${PRIMO_EXPECTED_REVISION:?}" --out "${PRIMO_INCREMENTAL_OUT:?}" \
  --timeout 30
