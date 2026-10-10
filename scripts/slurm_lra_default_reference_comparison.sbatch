#!/usr/bin/env bash
# QF_LRA selection comparison: one automatic Primo profile and four references.
# Timing tasks interleave solvers: task = shard * 5 + solver_index.
# Model replay uses only tasks 0,5,... and immutable parent provenance.
#SBATCH --job-name=primo-lra-default-refs
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
cd "${PRIMO_WORKDIR:?}"
task=${SLURM_ARRAY_TASK_ID:?}
shards=${PRIMO_SHARDS:?}
shard=$((task / 5))
profile=$((task % 5))
if (( task < 0 || shard >= shards )); then
  echo 'array task outside the five-solver shard range' >&2
  exit 2
fi
case "$profile" in
  0) name=primo; solver=${PRIMO_SOLVER_DEFAULT:-build-slurm-rel/primo}; args=--quiet ;;
  1) name=opensmt; solver=contrib/local/bin/opensmt; args= ;;
  2) name=yices; solver=contrib/local/bin/yices-smt2; args= ;;
  3) name=z3; solver=contrib/local/bin/z3; args= ;;
  4) name=cvc5; solver=contrib/local/bin/cvc5; args= ;;
esac
case "${PRIMO_PHASE:?}" in
  benchmark) ;;
  replay)
    if (( profile != 0 )); then
      echo 'model replay accepts only the Primo profile' >&2
      exit 2
    fi
    ;;
  *) echo 'invalid benchmark/replay phase' >&2; exit 2 ;;
esac
export PRIMO_SOLVER="$solver"
export PRIMO_SOLVER_ARGS="$args"
if (( profile == 0 )); then
  : "${PRIMO_EXPECTED_GIT:?}"
else
  unset PRIMO_EXPECTED_GIT
fi
export PRIMO_CAMPAIGN_OUT="${PRIMO_CAMPAIGN_OUT:?}/$name"
export SLURM_ARRAY_TASK_ID="$shard"
export PRIMO_ORIGINAL_ARRAY_TASK="$task"
export PRIMO_DOLMEN_SIZE=${PRIMO_DOLMEN_SIZE:-5G}
export PRIMO_MODEL_TIMEOUT=${PRIMO_MODEL_TIMEOUT:-3600}
export PRIMO_DOLMEN_TIMEOUT=${PRIMO_DOLMEN_TIMEOUT:-120}
python3 - <<'ENVIRONMENT'
import json
import os
import pathlib
import subprocess

models = {
    line.split(':', 1)[1].strip()
    for line in pathlib.Path('/proc/cpuinfo').read_text().splitlines()
    if line.startswith('model name')
}
if len(models) != 1 or not next(iter(models)).startswith('Intel(R) Xeon(R) CPU E5-2690 v4'):
    raise SystemExit(f'refusing mismatched comparison CPU: {models}')
out = pathlib.Path(os.environ['PRIMO_CAMPAIGN_OUT']) / f"shard-{os.environ['SLURM_ARRAY_TASK_ID']}"
out.mkdir(parents=True, exist_ok=True)
data = {
    'cpu_models': sorted(models),
    'host': subprocess.check_output(['hostname'], text=True).strip(),
    'affinity': sorted(os.sched_getaffinity(0)),
    'job': os.environ['SLURM_JOB_ID'],
    'array_job': os.environ['SLURM_ARRAY_JOB_ID'],
    'array_task': int(os.environ['PRIMO_ORIGINAL_ARRAY_TASK']),
    'partition': os.environ.get('SLURM_JOB_PARTITION'),
    'memory_limit': os.environ.get('SLURM_MEM_PER_NODE', 'unspecified') + ' MiB',
    'nomultithread': True,
    'phase': os.environ['PRIMO_PHASE'],
}
(out / ('execution-' + os.environ['PRIMO_PHASE'] + '.json')).write_text(json.dumps(data, indent=2) + '\n')
ENVIRONMENT
exec bash scripts/slurm_real_corpus_validation.sbatch
