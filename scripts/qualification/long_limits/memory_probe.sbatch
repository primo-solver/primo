#!/usr/bin/env bash
#SBATCH --job-name=primo-memory-probe
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#SBATCH --time=00:05:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
if [[ ${PRIMO_ENFORCE_AS:-0} == 1 ]]; then
  ulimit -v 8388608
fi
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
from pathlib import Path
import errno, hashlib, json, mmap, os, platform, resource, subprocess, sys
memberships = [line.split(':', 2) for line in Path('/proc/self/cgroup').read_text().splitlines()]
records = []
for line in Path('/proc/self/mountinfo').read_text().splitlines():
    left, right = line.split(' - ', 1)
    fields, details = left.split(), right.split()
    filesystem = details[0]
    if filesystem not in {'cgroup', 'cgroup2'}:
        continue
    for _, controllers, group in memberships:
        if filesystem == 'cgroup' and ('memory' not in controllers.split(',') or 'memory' not in details[-1].split(',')):
            continue
        if filesystem == 'cgroup2' and controllers:
            continue
        mount, mount_root = Path(fields[4]), Path(fields[3])
        try:
            directory = mount / Path(group).relative_to(mount_root)
        except ValueError:
            continue
        while directory.is_relative_to(mount):
            values = {}
            for name in ('memory.limit_in_bytes', 'memory.memsw.limit_in_bytes', 'memory.use_hierarchy',
                         'memory.max_usage_in_bytes', 'memory.failcnt', 'memory.oom_control',
                         'memory.stat', 'memory.max', 'memory.swap.max', 'memory.events', 'memory.peak'):
                path = directory / name
                if path.exists():
                    text = path.read_text()
                    if name == 'memory.stat':
                        text = '\n'.join(row for row in text.splitlines() if 'hierarchical' in row)
                    values[name] = text.strip()
            if values:
                records.append({'filesystem': filesystem, 'path': str(directory), 'values': values})
            if directory == mount:
                break
            directory = directory.parent
report = {'job': os.environ['SLURM_JOB_ID'], 'host': platform.node(),
          'requested_memory_mib': os.environ.get('SLURM_MEM_PER_NODE'),
          'memberships': memberships, 'address_space_rlimit': resource.getrlimit(resource.RLIMIT_AS),
          'cgroups': records}
out = Path('results/long-limits-20261022/run1') / ('memory-probe-' + os.environ['SLURM_JOB_ID'] + '.json')
if os.environ.get('PRIMO_ENFORCE_AS') == '1':
    assert resource.getrlimit(resource.RLIMIT_AS) == (8 * 1024**3, 8 * 1024**3)
    try:
        allocation = mmap.mmap(-1, 9 * 1024**3)
    except OSError as error:
        assert error.errno == errno.ENOMEM
        report['oversized_virtual_mapping'] = 'rejected-with-ENOMEM'
    else:
        allocation.close()
        raise AssertionError('9GiB mapping succeeded under an 8GiB limit')
    base = Path(os.environ['PRIMO_MEMORY_BASE'])
    inventory = json.loads((base / 'inventory/UF.json').read_text())
    name = 'sledgehammer/FFT/uf.557340.smt2'
    original = Path(inventory['root']) / name
    assert hashlib.sha256(original.read_bytes()).hexdigest() == inventory['inputs'][name]['sha256']
    native = base / 'bin/primo'
    assert hashlib.sha256(native.read_bytes()).hexdigest() == 'e06bd36239ccc7af9fd29ed55af9f595f2e7cbd52f6ba5961b962c9d8d6fc26d'
    small = 64 * 1024**2
    command = [str(native), '--quiet', str(original)]
    process = subprocess.run(command, capture_output=True, text=True, timeout=30,
                             preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (small, small)))
    logs = out.parent / ('memory-native-smoke-' + os.environ['SLURM_JOB_ID'])
    logs.mkdir()
    (logs / 'stdout').write_text(process.stdout)
    (logs / 'stderr').write_text(process.stderr)
    sys.path.insert(0, str(Path(os.environ['PRIMO_LONG_DRIVER']).parent))
    from campaign import check_rows, memory_exhausted
    row = {'status': 'error', 'answer': 'error', 'wrong': 'false', 'exit_code': process.returncode,
           'stdout_log': str(logs / 'stdout'), 'stderr_log': str(logs / 'stderr')}
    assert process.returncode != 0, row
    check_rows([row], [], allow_execution_errors=True)
    report['native_reduced_limit_smoke'] = {'command': command, 'address_space_bytes': small,
                                          'row': row, 'classification': ('memory-out-unsolved' if memory_exhausted(row)
                                                             else 'native-error-under-forced-small-memory')}
out.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
PY
