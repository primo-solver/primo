#!/usr/bin/env bash
#SBATCH --job-name=primo-bound-static
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=12G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 - <<'PY'
import concurrent.futures
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path

out = Path(os.environ['PRIMO_REVIEW_OUT_DIR'])
out.mkdir(parents=True, exist_ok=True)
sources = sorted(p for root in ('src', 'tests', 'docs/examples') for p in Path(root).rglob('*.cpp'))
database = json.loads(Path('build-slurm-rel/compile_commands.json').read_text())
template = next(entry for entry in database if entry['file'].endswith('/src/main.cpp'))
for source in sources:
    if not str(source).startswith('docs/examples/'):
        continue
    arguments = shlex.split(template['command'])
    output_index = arguments.index('-o')
    del arguments[output_index:output_index + 2]
    arguments = [str(source.resolve()) if arg == template['file'] else arg for arg in arguments]
    arguments.append('-UNDEBUG')  # Standalone examples keep their assertions active.
    database.append({'directory': str(Path.cwd()), 'file': str(source.resolve()),
                     'arguments': arguments})
compile_db = out / 'compile-db'
compile_db.mkdir(exist_ok=True)
(compile_db / 'compile_commands.json').write_text(json.dumps(database, indent=2) + '\n')
(out / 'sources.txt').write_text(''.join(str(source) + '\n' for source in sources))
headers = Path('tmp/cxx-header-paths.txt').read_text().splitlines()
extra = [argument for path in headers for argument in ('--extra-arg=-isystem', '--extra-arg=' + path)]
# Adding one check retains clang-tidy's default analyzer checks. Save the
# effective list; both scans share the same parsed translation unit.
checks = subprocess.check_output(['clang-tidy', '-list-checks', '-checks=misc-include-cleaner'], text=True)
(out / 'enabled-checks.txt').write_text(checks)
assert 'clang-analyzer-core' in checks and 'misc-include-cleaner' in checks
(out / 'version.txt').write_text(subprocess.check_output(['clang-tidy', '--version'], text=True))

def scan(source):
    command = ['clang-tidy', '-p', str(compile_db), '-checks=misc-include-cleaner', *extra, str(source)]
    log = str(source).replace('/', '_') + '.log'
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    with (out / log).open('w') as stream:
        proc = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=600, check=False)
    assert before == hashlib.sha256(source.read_bytes()).hexdigest()
    return {'file': str(source), 'sha256': before, 'exit_code': proc.returncode, 'log': log}

rows = []
with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ['SLURM_CPUS_PER_TASK'])) as pool:
    for row in pool.map(scan, sources):
        rows.append(row)
        (out / 'progress.json').write_text(json.dumps(rows, indent=2) + '\n')
        print(row['file'], row['exit_code'], flush=True)
(out / 'summary.json').write_text(json.dumps(rows, indent=2) + '\n')
assert len(rows) == len(sources) and all(row['exit_code'] == 0 for row in rows)
PY
