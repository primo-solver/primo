#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-reference-panel
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --hint=nomultithread
#SBATCH --array=0-1%2
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PYTHONPATH="$PWD/scripts"
labels=(yices-smt2 opensmt)
label=${labels[${SLURM_ARRAY_TASK_ID:?}]}
reference="$PWD/tmp/reference-panel/$label"
root="${PRIMO_REF_INPUTS:?}"
out="${PRIMO_REF_OUT:?}/$label"
if [[ ${PRIMO_REF_PHASE:?} == models ]]; then
 if [[ -s $out/timing/sat-manifest.txt ]]; then
  python3 scripts/validate_model.py --solver "$reference" --expect-sat \
   --require-dolmen-pass --dolmen contrib/local/bin/dolmen --dolmen-size 2G \
   --dolmen-arg=--strict=false --timeout 90 --dolmen-timeout 30 \
   --manifest "$out/timing/sat-manifest.txt" --out "$out/models"
 fi
 exit
fi
export PRIMO_REF_BINARY="$reference"
export PRIMO_REF_LABEL="$label"
export PRIMO_REF_TIMING="$out/timing"
python3 - <<'PY'
import hashlib
import json
import os
import subprocess
from pathlib import Path
from bench_qf_uf_full import read_manifest, run_solver

root = Path(os.environ['PRIMO_REF_INPUTS']).resolve()
out = Path(os.environ['PRIMO_REF_TIMING'])
out.mkdir(parents=True, exist_ok=False)
reference = Path(os.environ['PRIMO_REF_BINARY'])
expected = json.loads((root / 'expected-statuses.json').read_text())
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
metadata = {
    'reference': os.environ['PRIMO_REF_LABEL'], 'binary_sha256': sha(reference),
    'version': subprocess.check_output([str(reference), '--version'], stderr=subprocess.STDOUT, text=True),
    'manifest_sha256': sha(root / 'manifest.txt'), 'expected_statuses_sha256': sha(root / 'expected-statuses.json'),
    'job': os.environ['SLURM_JOB_ID'], 'array_job': os.environ['SLURM_ARRAY_JOB_ID'],
    'host': os.environ['SLURMD_NODENAME'], 'affinity': sorted(os.sched_getaffinity(0)),
    'timeout_seconds': 60, 'records': [], 'complete': False,
}
for index, path in enumerate(read_manifest(root / 'manifest.txt', root)):
    before = sha(path)
    run = run_solver([str(reference)], path, 60)
    (out / f'case-{index:02d}.stdout').write_text(run.pop('stdout'))
    (out / f'case-{index:02d}.stderr').write_text(run.pop('stderr'))
    assert before == sha(path)
    name = str(path.relative_to(root))
    known = expected[name]
    wrong = (run['status'] == 'ok' and run['answer'] in ('sat', 'unsat')
             and known != 'unknown' and run['answer'] != known)
    metadata['records'].append({'file': name, 'sha256': before, 'expected': known,
                                'command': [str(reference), str(path)], 'run': run, 'wrong': wrong})
    (out / 'summary.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(name, run['status'], run['answer'], run['seconds'], flush=True)
    if wrong or run['status'] in ('error', 'unsupported'):
        raise SystemExit('reference discrepancy/error requires triage')
sat = [str(root / row['file']) for row in metadata['records']
       if row['run']['status'] == 'ok' and row['run']['answer'] == 'sat']
(out / 'sat-manifest.txt').write_text(''.join(p + '\n' for p in sat))
metadata['complete'] = True
(out / 'summary.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
