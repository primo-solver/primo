#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-selfstep-summary
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:10:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 scripts/summarize_soi_improvements.py --run "${PRIMO_TIMING_ROOT:?}" \
 --historical docs/coverage/lra-simplex-per-input-20261031.csv \
 --baseline old-combined-g1-f0 --baseline old-combined-g1-f1 \
 --out "${PRIMO_SUMMARY_OUT:?}"
PYTHONPATH=scripts python3 - <<'PY'
import os
from pathlib import Path
from benchmark_model_replay import digest, write_json
from summarize_soi_qualification import diagnostics, model_coverage

result = {
    'analysis_sha256': digest(Path('scripts/summarize_soi_qualification.py')),
    'models': model_coverage(Path(os.environ['PRIMO_TIMING_ROOT'])),
}
if os.environ.get('PRIMO_PROFILE_ROOT'):
    result['diagnostics'] = diagnostics(Path(os.environ['PRIMO_PROFILE_ROOT']))
write_json(Path(os.environ['PRIMO_QUALIFICATION_OUT']), result)
PY
