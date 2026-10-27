#!/usr/bin/env bash
# Stage this wrapper as a campaign artifact; keep the frozen solver workspace intact.
#SBATCH --job-name=primo-lra-ablation
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=7G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PWD/.venv-yinyang/bin:$PATH"
python3 scripts/qualification/lra_ablation/run.py "${PRIMO_PHASE:?}"
if [[ $PRIMO_PHASE == benchmark ]]; then
  python3 - <<'AUDIT'
import csv
import os
from pathlib import Path
import subprocess

task = int(os.environ['SLURM_ARRAY_TASK_ID'])
shard, profile = divmod(task, 4)
name = ('default', 'phase', 'monotone', 'both')[profile]
out = Path(os.environ['PRIMO_CAMPAIGN_OUT']) / name / f'shard-{shard}'
with (out / 'answers.csv').open() as stream:
    rows = list(csv.DictReader(stream))
bad = [r for r in rows if r['wrong'] == 'true' or r['status'] not in {'ok', 'timeout'}
       or (r['status'] == 'ok' and (r['exit_code'] != '0' or r['timeout'] != 'false'
                                  or r['answer'] not in {'sat', 'unsat', 'unknown'}))]
if bad:
    subprocess.run(['scancel', os.environ['SLURM_ARRAY_JOB_ID']], check=False)
    raise SystemExit(f'correctness failure; cancelled this timing array: {bad[:3]}')
AUDIT
fi
