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
reference_bin_dir=${PRIMO_REFERENCE_BIN_DIR:-contrib/local/bin}
if (( task < 0 || shard >= shards )); then
  echo 'array task outside the five-solver shard range' >&2
  exit 2
fi
case "$profile" in
  0) name=primo; solver=${PRIMO_SOLVER_DEFAULT:-build-slurm-rel/primo}; args=--quiet ;;
  1) name=opensmt; solver=$reference_bin_dir/opensmt; args= ;;
  2) name=yices; solver=$reference_bin_dir/yices-smt2; args= ;;
  3) name=z3; solver=$reference_bin_dir/z3; args= ;;
  4) name=cvc5; solver=$reference_bin_dir/cvc5; args= ;;
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
import hashlib
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
if os.environ['PRIMO_PHASE'] == 'benchmark':
    manifest = pathlib.Path(os.environ['PRIMO_MANIFEST'])
    checksum = hashlib.sha256(manifest.read_bytes()).hexdigest()
    names = [line.split('#', 1)[0].strip() for line in manifest.read_text().splitlines()]
    names = [name for name in names if name]
    if checksum != os.environ['PRIMO_MANIFEST_SHA256'] or len(names) != 519 or len(set(names)) != 519:
        raise SystemExit('selection manifest hash/count mismatch')
    data['manifest_sha256'] = checksum
    data['selected_instances'] = len(names)
(out / ('execution-' + os.environ['PRIMO_PHASE'] + '.json')).write_text(json.dumps(data, indent=2) + '\n')
ENVIRONMENT
command=(python3 scripts/benchmark_model_replay.py "$PRIMO_PHASE"
         --out "$PRIMO_CAMPAIGN_OUT/shard-$shard")
case "$PRIMO_PHASE" in
  benchmark)
    command+=(--root "${PRIMO_CORPUS_ROOT:?}" --solver "$PRIMO_SOLVER"
              --run-id "${PRIMO_CAMPAIGN_ID:?}-$name" --shards "$shards"
              --shard-index "$shard" --timeout "${PRIMO_TIMEOUT:?}"
              --manifest "${PRIMO_MANIFEST:?selection manifest required}")
    if (( profile == 0 )); then
      command+=(--expected-git "$PRIMO_EXPECTED_GIT" --solver-arg=--quiet)
    fi
    ;;
  replay)
    command+=(--model-timeout "$PRIMO_MODEL_TIMEOUT" --dolmen-timeout "$PRIMO_DOLMEN_TIMEOUT"
              --dolmen "${PRIMO_DOLMEN:-contrib/local/bin/dolmen}"
              --dolmen-size "$PRIMO_DOLMEN_SIZE" --dolmen-arg=--strict=false
              --attempt "${PRIMO_REPLAY_ATTEMPT:-model-validation}")
    ;;
esac
exec "${command[@]}"
