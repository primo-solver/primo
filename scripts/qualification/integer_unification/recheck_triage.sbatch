#!/usr/bin/env bash
# Recheck the same saved LRA assertions/models with explicit validator identity.
#SBATCH --job-name=primo-integer-triage-recheck
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=slurm_logs/%x-%j.out
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import hashlib, json, subprocess
from pathlib import Path

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

source = Path('results/integer-shared-paired/lra-model-triage/rows.json')
validator = Path('contrib/local/bin/dolmen').resolve()
identity = {'path': str(validator), 'sha256': sha(validator),
            'version': subprocess.check_output([str(validator), '--version'], text=True).strip()}
assert identity['sha256'] == '08b5fc3b17fb7eb54cb82141e9cabaf01039c686b2ac2512363344fbf23c052e'
rows = json.loads(source.read_text())
assert len(rows) == 20
results = []
for row in rows:
    command = row['command']
    model, problem = Path(command[3]), Path(command[-1])
    assert command == ['contrib/local/bin/dolmen', '--check-model=true', '-r',
                       str(model), '--strict=false', '--force-smtlib2-logic=QF_NRA',
                       '--size=2G', str(problem)]
    assert sha(problem) == row['input_sha256'] and sha(model) == row['model_sha256']
    command = [str(validator), *command[1:]]
    proc = subprocess.run(command, capture_output=True, text=True, timeout=30)
    results.append({**row, 'command': command, 'exit': proc.returncode,
                    'stdout': proc.stdout, 'stderr': proc.stderr})
assert sha(validator) == identity['sha256']
report = {'validator': identity, 'cwd': str(Path.cwd()),
          'source_sha256': sha(source), 'rows': results,
          'status': 'complete' if all(r['exit'] == 0 for r in results) else 'failed',
          'note': 'Same saved assertions/models; wider QF_NRA validator setting, not direct LRA passes.'}
Path('results/integer-model-audit-reviewed/triage-recheck.json').write_text(json.dumps(report, indent=2) + '\n')
print(report['status'], len(results), flush=True)
raise SystemExit(report['status'] != 'complete')
PY
