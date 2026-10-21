#!/usr/bin/env bash
# Cancel only this timing array if a completed shard reports a hard error.
#SBATCH --job-name=primo-lra-equality-timing
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=7G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 scripts/qualification/lra_equalities/campaign.py benchmark
python3 - <<'AUDIT'
import csv
import os
from pathlib import Path
import subprocess

task = int(os.environ['SLURM_ARRAY_TASK_ID'])
shard, profile = divmod(task, 4)
name = ('default', 'equality', 'both', 'both-equality')[profile]
out = Path(os.environ['PRIMO_CAMPAIGN_OUT']) / name / f'shard-{shard}'
with (out / 'answers.csv').open() as stream:
    rows = list(csv.DictReader(stream))
bad = [r for r in rows if r['wrong'] == 'true' or r['status'] not in {'ok', 'timeout'}
       or (r['status'] == 'ok' and (r['exit_code'] != '0' or r['timeout'] != 'false'
                                  or r['answer'] not in {'sat', 'unsat', 'unknown'}))]
if bad:
    subprocess.run(['scancel', os.environ['SLURM_ARRAY_JOB_ID']], check=False)
    raise SystemExit(f'correctness failure; cancelled only this timing array: {bad[:3]}')
AUDIT
