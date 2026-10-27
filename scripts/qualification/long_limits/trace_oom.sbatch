#!/usr/bin/env bash
# Diagnostic replay only: debugger has 16 GiB AS; the unchanged target has 8 GiB.
#SBATCH --job-name=primo-oom-exception
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=16G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
ulimit -v 16777216
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import signal
import subprocess


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


base = Path(os.environ['PRIMO_LONG_OUT'])
cases_path = Path(os.environ['PRIMO_OOM_CASES'])
case = json.loads(cases_path.read_text())[int(os.environ['SLURM_ARRAY_TASK_ID'])]
assert os.environ['SLURM_JOB_PARTITION'] == 'compute'
assert os.environ['SLURM_CPUS_PER_TASK'] == '1'
assert resource.getrlimit(resource.RLIMIT_AS) == (16 * 1024**3,) * 2
inventory = json.loads((base / 'inventory' / (case['logic'] + '.json')).read_text())
target = base / 'bin/primo'
assert sha(target) == 'e06bd36239ccc7af9fd29ed55af9f595f2e7cbd52f6ba5961b962c9d8d6fc26d'
source = Path(inventory['root']) / case['file']
assert sha(source) == inventory['inputs'][case['file']]['sha256']
out = Path(os.environ['PRIMO_OOM_OUT']) / (os.environ['SLURM_ARRAY_JOB_ID'] + '-' + os.environ['SLURM_ARRAY_TASK_ID'])
out.mkdir(parents=True)
os.environ['PRIMO_OOM_RECORD'] = str(out / 'inferior.json')
commands = out / 'commands.gdb'
commands.write_text('''set pagination off
set confirm off
set disable-randomization off
set exec-wrapper /usr/bin/prlimit --as=8589934592:8589934592
catch throw
run
python
import gdb, json, os
from pathlib import Path
pid = gdb.selected_inferior().pid
record = {'pid': pid}
for key in ('status', 'limits'):
    try:
        record[key] = Path('/proc/' + str(pid) + '/' + key).read_text()
    except OSError as error:
        record[key] = str(error)
for command in ('p $_exception', 'bt 24'):
    try:
        record[command] = gdb.execute(command, to_string=True)
    except gdb.error as error:
        record[command] = str(error)
print(json.dumps(record, indent=2))
Path(os.environ['PRIMO_OOM_RECORD']).write_text(json.dumps(record, indent=2) + '\\n')
end
continue
bt 24
''')
# This cluster's GDB10 uses DEBUGINFOD_URLS rather than a 'set debuginfod' command.
command = ['gdb', '-nx', '--batch', '-x', str(commands), '--args',
           str(target), *case['arguments'], str(source)]
with (out / 'stdout').open('w') as stdout, (out / 'stderr').open('w') as stderr:
    process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True,
                               env={**os.environ, 'DEBUGINFOD_URLS': ''})
    try:
        code = process.wait(timeout=720)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        raise
metadata = {'case': case, 'command': command, 'job': os.environ['SLURM_JOB_ID'],
            'array_job': os.environ['SLURM_ARRAY_JOB_ID'], 'host': platform.node(),
            'partition': os.environ['SLURM_JOB_PARTITION'], 'debugger_as_bytes': resource.getrlimit(resource.RLIMIT_AS),
            'target_as_bytes': [8589934592, 8589934592], 'native_sha256': sha(target),
            'input_sha256': sha(source), 'script_sha256': sha(Path(os.environ['PRIMO_OOM_SCRIPT'])),
            'cases_sha256': sha(cases_path), 'debugger_exit': code,
            'scope': 'Exception/abort diagnostic replay; no timing or model-qualification evidence.'}
(out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
assert (out / 'inferior.json').exists(), 'target did not stop at an exception'
print(json.dumps(metadata, indent=2))
PY
