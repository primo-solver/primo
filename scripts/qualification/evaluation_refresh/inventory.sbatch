#!/usr/bin/env bash
# Hash the exact corpus and preserved tools before any timing jobs.
#SBATCH --job-name=primo-refresh-inventory
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
mkdir -p results/refresh-inventory
python3 - <<'PY'
import hashlib
import json
import os
import platform
import subprocess
from pathlib import Path

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

report = {'host': platform.node(), 'job': os.environ['SLURM_JOB_ID'],
          'cpu': subprocess.check_output(['lscpu'], text=True), 'corpora': {}, 'tools': {}}
for logic in ('QF_UF', 'QF_LRA', 'QF_LIA', 'QF_UFLRA', 'QF_UFLIA', 'UF'):
    root = (Path('corpora') / logic).resolve(strict=True)
    inputs = {str(p.relative_to(root)): {'bytes': p.stat().st_size, 'sha256': digest(p)}
              for p in sorted(root.rglob('*.smt2')) if p.is_file()}
    if not inputs:
        raise ValueError(f'empty corpus: {logic}')
    report['corpora'][logic] = {'root': str(root), 'count': len(inputs),
                               'bytes': sum(p['bytes'] for p in inputs.values()),
                               'inputs': inputs}
for label in ('primo', 'z3', 'cvc5', 'dolmen'):
    path = Path('build-slurm-rel/primo' if label == 'primo' else f'contrib/local/bin/{label}')
    report['tools'][label] = {'sha256': digest(path), 'version': subprocess.check_output(
        [str(path), '--version'], text=True)}
assert f"git: {os.environ['PRIMO_EXPECTED_REVISION']}" in report['tools']['primo']['version'].splitlines()
Path('results/refresh-inventory/inventory.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({logic: {k: value[k] for k in ('count', 'bytes')}
                  for logic, value in report['corpora'].items()}, indent=2))
PY
