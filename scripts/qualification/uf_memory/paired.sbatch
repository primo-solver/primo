#!/usr/bin/env bash
# One input per CPU allocation, alternating paired frozen binaries.
#SBATCH --job-name=primo-uf-memory
#SBATCH --partition=compute
#SBATCH --array=0-5%3
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=10G
#SBATCH --time=00:22:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
"""CPU SLURM paired measurement. The controller executes no solver outside SLURM."""
import hashlib
import json
import os
import platform
import re
import resource
import signal
import subprocess
import time
from pathlib import Path


def sha(path):
    with path.open('rb') as stream:
        result = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def write(path, data):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2) + '\n')
    temporary.replace(path)


def classify(stdout, stderr, code, timed_out):
    answers = [line.strip() for line in stdout.splitlines()
               if line.strip() in ('sat', 'unsat', 'unknown', 'unsupported')]
    answer = answers[0] if len(answers) == 1 else None
    if timed_out:
        status = 'timeout'
    elif code != 0:
        if 'bad_alloc' in stderr or 'Cannot allocate memory' in stderr:
            status = 'explicit-allocation-failure'
        elif 'disconnect_external_propagator' in stderr and 'invalid state' in stderr:
            status = 'cleanup-abort'
        else:
            status = 'error'
    elif answer in ('sat', 'unsat'):
        status = 'solved'
    elif answer in ('unknown', 'unsupported'):
        status = answer
    else:
        status = 'error'
    return status, answer


def measure(command, directory, timeout, as_bytes, interval=0.05):
    """Sample the native process; wait4 supplies exact per-process max RSS/CPU."""
    directory.mkdir(parents=True, exist_ok=False)
    target = Path(command[0]).resolve()
    maximum = {}
    observed_limits = None
    timed_out = False
    samples = 0
    process = None
    usage = None
    start = time.monotonic()

    def limits():
        resource.setrlimit(resource.RLIMIT_AS, (as_bytes, as_bytes))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    try:
        with (directory / 'stdout').open('w') as stdout, (directory / 'stderr').open('w') as stderr:
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                       preexec_fn=limits, start_new_session=True)
            terminated_at = None
            while True:
                try:
                    proc = Path('/proc') / str(process.pid)
                    if (proc / 'exe').resolve() == target:
                        fields = dict(re.findall(r'^(VmPeak|VmSize|VmHWM|VmRSS|VmSwap):\s+(\d+) kB$',
                                                 (proc / 'status').read_text(), re.MULTILINE))
                        samples += 1
                        for name, value in fields.items():
                            maximum[name] = max(maximum.get(name, 0), int(value))
                        if observed_limits is None:
                            observed_limits = (proc / 'limits').read_text()
                            wanted = rf'Max address space\s+{as_bytes}\s+{as_bytes}\s+bytes'
                            if not re.search(wanted, observed_limits):
                                raise RuntimeError('observed target RLIMIT_AS differs from plan')
                except (FileNotFoundError, ProcessLookupError):
                    pass
                pid, wait_status, child_usage = os.wait4(process.pid, os.WNOHANG)
                if pid:
                    process.returncode = os.waitstatus_to_exitcode(wait_status)
                    usage = child_usage
                    break
                now = time.monotonic()
                if not timed_out and now - start >= timeout:
                    timed_out = True
                    terminated_at = now
                    os.killpg(process.pid, signal.SIGTERM)
                elif terminated_at is not None and now - terminated_at >= 2:
                    os.killpg(process.pid, signal.SIGKILL)
                    terminated_at = None
                time.sleep(interval)
    finally:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            if process.returncode is None:
                _, wait_status, usage = os.wait4(process.pid, 0)
                process.returncode = os.waitstatus_to_exitcode(wait_status)
    elapsed = time.monotonic() - start
    stdout = (directory / 'stdout').read_text()
    stderr = (directory / 'stderr').read_text()
    status, answer = classify(stdout, stderr, process.returncode, timed_out)
    statistics = {name: int(value) for name, value in
                  re.findall(r'^(:[\w.-]+)\s+(-?\d+)\s*$', stderr, re.MULTILINE)}
    record = {'command': command, 'status': status, 'answer': answer,
              'exit_code': process.returncode, 'timed_out': timed_out,
              'wall_seconds': elapsed, 'user_seconds': usage.ru_utime,
              'system_seconds': usage.ru_stime, 'max_rss_kib': usage.ru_maxrss,
              'minor_faults': usage.ru_minflt, 'major_faults': usage.ru_majflt,
              'voluntary_switches': usage.ru_nvcsw, 'involuntary_switches': usage.ru_nivcsw,
              'sampled_proc_maxima_kib': maximum, 'samples': samples,
              'sample_interval_seconds': interval, 'as_limit_bytes': as_bytes,
              'observed_limits': observed_limits, 'statistics': statistics,
              'stats_available': bool(statistics)}
    write(directory / 'result.json', record)
    return record


def main():
    if os.environ.get('SLURM_JOB_PARTITION') != 'compute':
        raise RuntimeError('requires CPU SLURM compute partition')
    plan_path = Path(os.environ['PRIMO_UF_PLAN'])
    plan = json.loads(plan_path.read_text())
    task = int(os.environ['SLURM_ARRAY_TASK_ID'])
    case = plan['cases'][task]
    root = Path(os.environ['PRIMO_UF_ROOT' if case['logic'] == 'UF' else 'PRIMO_QF_UF_ROOT'])
    source = root / case['file']
    if sha(source) != case['sha256']:
        raise RuntimeError('input differs from frozen manifest')
    out = Path(os.environ['PRIMO_UF_OUT']) / os.environ['SLURM_ARRAY_JOB_ID'] / f'case-{task}'
    out.mkdir(parents=True, exist_ok=False)
    binaries = {name: Path(os.environ['PRIMO_UF_' + name.upper()]).resolve()
                for name in ('baseline', 'fixed')}
    expected = {'baseline': '3b464f63254ff87309c18850f3ea2e3f36bdd45c82cf97842faa5434e0f3e901',
                'fixed': '4a680d24feef6b49947bf1ec6610170b78bc6d7307ccce055a1874d41bee40d4'}
    before = {name: sha(binary) for name, binary in binaries.items()}
    if before != expected:
        raise RuntimeError('binary fingerprint differs from qualified pair')
    affinity = os.sched_getaffinity(0)
    os.sched_setaffinity(0, {min(affinity)})
    metadata = {'case': case, 'plan_sha256': sha(plan_path), 'binary_sha256': before,
                'versions': {name: subprocess.run([str(binary), '--version'], check=True,
                    capture_output=True, text=True).stdout for name, binary in binaries.items()},
                'job': os.environ['SLURM_JOB_ID'], 'host': platform.node(),
                'cpu_models': sorted({line.split(':', 1)[1].strip() for line in
                    Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')}),
                'affinity_before': sorted(affinity), 'affinity': sorted(os.sched_getaffinity(0)),
                'partition': os.environ['SLURM_JOB_PARTITION'], 'repetitions': plan['repetitions'],
                'args': plan['solver_args'], 'timeout_seconds': plan['timeout_seconds'],
                'scope': 'Focused paired diagnostic; no full-corpus claim. wait4 RSS is process peak; VmPeak and VmSize are sampled lower bounds. Flat-term counters are cumulative, not peak resident nodes.'}
    write(out / 'metadata.json', metadata)
    records = []
    for repetition in range(plan['repetitions']):
        order = ['baseline', 'fixed']
        if (task + repetition) % 2:
            order.reverse()
        for profile in order:
            if sha(binaries[profile]) != expected[profile] or sha(source) != case['sha256']:
                raise RuntimeError('frozen binary/input changed')
            result = measure([str(binaries[profile]), *plan['solver_args'], str(source)],
                             out / f'round-{repetition}-{profile}', plan['timeout_seconds'],
                             plan['as_limit_bytes'])
            if result['observed_limits'] is None:
                raise RuntimeError('native address-space limit was not observed')
            row = {'profile': profile, 'repetition': repetition, **result}
            records.append(row)
            write(out / 'summary.json', records)
            print(json.dumps({key: row[key] for key in
                              ('profile', 'repetition', 'status', 'wall_seconds', 'max_rss_kib')}), flush=True)
            if result['status'] == 'solved' and case['expected'] in ('sat', 'unsat'):
                if result['answer'] != case['expected']:
                    raise RuntimeError('declared-status disagreement: stop performance analysis')
            if result['status'] == 'error':
                raise RuntimeError('untriaged execution error; retain artifacts')
        pair = records[-2:]
        if all(row['status'] == 'solved' for row in pair) and pair[0]['answer'] != pair[1]['answer']:
            raise RuntimeError('paired answer disagreement: stop performance analysis')
    after = {name: sha(binary) for name, binary in binaries.items()}
    if after != before:
        raise RuntimeError('binary changed during measurements')
    write(out / 'complete.json', {'runs': len(records), 'binary_sha256': after})


if __name__ == '__main__':
    main()
PY
