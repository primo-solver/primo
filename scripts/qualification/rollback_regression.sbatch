#!/usr/bin/env bash
# Link the new rollback regression against the previously qualified native libraries.
#SBATCH --job-name=primo-rollback-baseline
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
ulimit -c 0
cd "${PRIMO_WORKDIR:?}"
export PRIMO_BASELINE_BUILD=${PRIMO_BASELINE_BUILD:?}
out="results/diagnostic-qualification/baseline-$SLURM_JOB_ID"
export PRIMO_REGRESSION_OUT="$PWD/$out"
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/stdout.log") 2> >(tee "$out/stderr.log" >&2)
trap 'status=$?; printf "%s\n" "$status" > "$out/exit-code"' EXIT
python3 - <<'PY'
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path

baseline = Path(os.environ['PRIMO_BASELINE_BUILD']).resolve()
out = Path(os.environ['PRIMO_REGRESSION_OUT'])
source = Path('tests/unit/euf_solver_tests.cpp').resolve()
rows = json.loads((baseline / 'compile_commands.json').read_text())
row = next(row for row in rows if row['file'].endswith('/tests/unit/euf_solver_tests.cpp'))
old_source = row['file']
obj = out / 'regression.o'
command = shlex.split(row['command']) if 'command' in row else list(row['arguments'])
command[command.index(old_source)] = str(source)
command[command.index('-o') + 1] = str(obj)
subprocess.run(command, cwd=row['directory'], check=True)
link = shlex.split((baseline / 'CMakeFiles/primo_euf_solver_tests.dir/link.txt').read_text())
objects = [index for index, arg in enumerate(link) if arg.endswith('euf_solver_tests.cpp.o')]
assert len(objects) == 1
link[objects[0]] = str(obj)
binary = out / 'baseline-with-regression'
link[link.index('-o') + 1] = str(binary)
subprocess.run(link, cwd=baseline, check=True)
result = subprocess.run(['timeout', '-k', '2s', '120s', str(binary)],
                        capture_output=True, text=True, check=False)
(out / 'test.stdout').write_text(result.stdout)
(out / 'test.stderr').write_text(result.stderr)
report = {'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'baseline_library_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                     for path in sorted(baseline.glob('libprimo_*.a'))},
          'compile': command, 'link': link, 'test_exit': result.returncode,
          'expected': 'SIGABRT from the added disabled-proof-storage regression',
          'scope': 'New EUF unit-test translation unit linked to preserved pre-fix libraries.'}
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
assert result.returncode in (-6, 134), report
print('Pre-fix libraries reproduce the regression:', result.returncode)
PY
