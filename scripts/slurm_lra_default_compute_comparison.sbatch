#!/usr/bin/env bash
# Candidate speed comparison on one homogeneous CPU generation.
#SBATCH --job-name=primo-lra-compute-profile
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=7G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 - <<'ENVIRONMENT'
import json,os,pathlib,subprocess
task=int(os.environ['SLURM_ARRAY_TASK_ID'])
shards=int(os.environ['PRIMO_SHARDS'])
profile=task//shards
if profile not in (0,6):
    raise SystemExit('candidate comparison admits only previous and automatic defaults')
name='legacy-default' if profile==0 else 'default'
out=pathlib.Path(os.environ['PRIMO_CAMPAIGN_OUT'])/name/f'shard-{task%shards}'
out.mkdir(parents=True,exist_ok=True)
models={line.split(':',1)[1].strip() for line in pathlib.Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')}
if len(models)!=1 or not next(iter(models)).startswith('Intel(R) Xeon(R) CPU E5-2690 v4'):
    raise SystemExit(f'refusing mismatched timing CPU: {models}')
data={'cpu_models':sorted(models),'host':subprocess.check_output(['hostname'],text=True).strip(),
      'affinity':sorted(os.sched_getaffinity(0)),
      'job':os.environ['SLURM_JOB_ID'],'array_job':os.environ['SLURM_ARRAY_JOB_ID'],
      'array_task':task,'partition':os.environ.get('SLURM_JOB_PARTITION'),
      'memory_limit':'7GiB','nomultithread':True,'phase':os.environ['PRIMO_PHASE']}
(out/('execution-'+os.environ['PRIMO_PHASE']+'.json')).write_text(json.dumps(data,indent=2)+'\n')
ENVIRONMENT
exec bash scripts/slurm_lra_profile_comparison.sbatch
