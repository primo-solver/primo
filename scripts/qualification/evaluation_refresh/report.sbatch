#!/usr/bin/env bash
# Metadata only: submit after all timing and model-replay arrays terminate.
#SBATCH --job-name=primo-refresh-report
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=results/refresh-report-%j.out
#SBATCH --error=results/refresh-report-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

reporting = Path(os.environ['PRIMO_REPORTING_DIR'])
# The reporting package is separate from the frozen measured source/build.
command = [sys.executable, str(reporting / 'scripts/qualification/evaluation_refresh/summarize.py'),
           '--root', 'results/refresh-timing', '--inventory', 'results/refresh-inventory/inventory.json',
           '--reference-map', 'results/refresh-reference-map.json', '--timeout', '60',
           '--out', 'results/refresh-timing-summary.json']
code = subprocess.run(command, check=False).returncode
status = {'single_query_report_exit': code, 'model_coverage_complete': False, 'native': {}}
if code == 0:
    ground = json.loads(Path('results/refresh-timing-summary.json').read_text())
    status['model_coverage_complete'] = all(
        route['runs']['primo']['models']['fully_validated']
        for logic, route in ground['routes'].items() if logic != 'UF')

spec = importlib.util.spec_from_file_location('native', reporting / 'scripts/summarize_native_uf.py')
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
references = json.loads(Path('results/refresh-native-reference-audit.json').read_text())
for route, expected, families in [('uf', 1778, None), ('lra', 10, ['hybrid_networks'])]:
    root = Path(f'results/refresh-native-{route}')
    data = {label: native.load_rows(root / 'timing' / label) for label in native.LABELS[:2]}
    # Cancelled reference timing rows never enter either report.
    for label in native.LABELS[2:]:
        data[label] = native.load_rows(Path(references[label]['root'])) if route == 'uf' else {}
    models = {label: native.load_rows(root / 'validation' / label) for label in native.LABELS[:2]}
    result = native.summarize(data, models, expected, families)
    result['reference_origin'] = 'audited historical' if route == 'uf' else 'unavailable'
    result['source_timing_root'] = str(root / 'timing')
    result['source_validation_root'] = str(root / 'validation')
    result['scope'] = 'Whole-session timings; incomplete queries are not complete sessions.'
    (root / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    profiles = [result['profiles'][label] for label in native.LABELS[:2]]
    status['native'][route] = {
        'timing_complete': all(p['full_archive_timing'] for p in profiles),
        'answer_failures': any(p['outcomes'].get('error', 0) or
                              p['outcomes'].get('wrong-answer', 0) for p in profiles),
        'reference_disagreements': result['reference_disagreements'],
        'model_coverage_complete': all(
            not p['validation']['complete_without_qualification'] and
            not p['validation']['changed_answers'] and not p['validation']['failures']
            for p in profiles),
    }
Path('results/refresh-report-status.json').write_text(json.dumps(status, indent=2) + '\n')
print(json.dumps(status, indent=2))
raise SystemExit(int(code != 0 or not status['model_coverage_complete'] or any(
    not s['timing_complete'] or s['answer_failures'] or s['reference_disagreements']
    or not s['model_coverage_complete'] for s in status['native'].values())))
PY
