#!/usr/bin/env bash
# Reuse freshly verified parser boundaries to describe unchanged native sessions.
#SBATCH --job-name=primo-refresh-native-manifest
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import json
from pathlib import Path
source = json.loads(Path('benchmarks/native-incremental-qf-lra.json').read_text())
for index, entry in enumerate(source['entries']):
    folder = Path('results/native-lra-qualification') / f'task-{index}'
    result = json.loads((folder / 'result.json').read_text())
    run = json.loads((folder / 'run.json').read_text())
    if (run['entry'] != entry or result['parser']['outcome'] != 'pass'
            or not result['inputs_unchanged'] or not result['binaries_unchanged']
            or result['session']['mismatches'] or result['model_prefix']['outcome'] != 'pass'):
        raise ValueError(f'unqualified native input: {index}')
    answers = json.loads((folder / 'oracle.json').read_text())
    if len(answers) != entry['queries'] or set(answers) - {'sat', 'unsat', 'unknown'}:
        raise ValueError(f'invalid native query metadata: {index}')
    # Unknown annotations remain unknown; fresh references must check those queries.
    entry['answers'] = answers
Path('results/native-lra-manifest.json').write_text(json.dumps(source, indent=2) + '\n')
print(f"prepared {len(source['entries'])} native sessions")
PY
