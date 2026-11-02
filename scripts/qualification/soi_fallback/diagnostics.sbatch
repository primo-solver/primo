#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-fallback-diagnostics
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
PYTHONPATH=scripts python3 - <<'PY'
import os
from pathlib import Path
from benchmark_model_replay import digest, write_json
from summarize_soi_qualification import diagnostics
write_json(Path(os.environ['PRIMO_DIAGNOSTICS_OUT']), {
    'analysis_sha256': digest(Path('scripts/summarize_soi_qualification.py')),
    'diagnostics': diagnostics(Path(os.environ['PRIMO_PROFILE_ROOT'])),
})
PY
