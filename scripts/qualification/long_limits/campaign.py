#!/usr/bin/env python3
"""Stage, run and audit matched long-limit comparisons using the common harness."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import resource
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def write(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def load(path: Path):
    return json.loads(path.read_text())


def journal(path: Path, *, live: bool = False) -> list[dict]:
    try:
        if not path.exists():
            return []
        return [json.loads(line) for line in path.read_text().splitlines(keepends=True)
                if line.endswith('\n')]
    except (OSError, UnicodeError, json.JSONDecodeError):
        if not live:
            raise
        # A concurrent network-filesystem snapshot is advisory. The terminal
        # CSV is checked strictly before this task can succeed or be reused.
        return []


class CorrectnessFailure(ValueError):
    """A wrong answer/contradiction, or an error rejected by a strict audit caller."""


def memory_exhausted(row: dict) -> bool:
    if row['status'] != 'error':
        return False
    diagnostic = str(row.get('stderr_head', ''))
    for key in ('stdout_log', 'stderr_log'):
        if row.get(key):
            with Path(row[key]).open(errors='replace') as stream:
                diagnostic += '\n' + stream.read(16384)
    lines = [line.strip().lower() for line in diagnostic.splitlines()]
    return any(line.startswith(('error: std::bad_alloc', '(error "std::bad_alloc"',
                                'std::bad_alloc', 'what():  std::bad_alloc',
                                'error: out of memory', '(error "out of memory')) for line in lines)


def decisive(row: dict) -> bool:
    return (row['status'] == 'ok' and row['answer'] in {'sat', 'unsat'}
            and str(row['exit_code']) == '0' and str(row['timeout']).lower() == 'false')


def check_rows(rows: list[dict], peers: list[dict], *, reference: bool = False,
               allow_execution_errors: bool = False) -> None:
    for row in rows:
        allowed = {'ok', 'timeout', 'error', 'unsupported'} if reference or allow_execution_errors else {'ok', 'timeout'}
        resource_failure = memory_exhausted(row)
        if (row['wrong'] == 'true' or (row['status'] not in allowed and not resource_failure)
                or (row['status'] == 'ok' and
                    (str(row['exit_code']) != '0' or str(row['timeout']).lower() != 'false'
                     or row['answer'] not in {'sat', 'unsat', 'unknown'}))):
            raise CorrectnessFailure(f'hard answer failure: {row}')
    answers = {r['file']: r['answer'] for r in rows if decisive(r)}
    for row in peers:
        if decisive(row) and row['file'] in answers and row['answer'] != answers[row['file']]:
            raise CorrectnessFailure(f'decisive solver disagreement: {row}')


def recovery(out: Path, campaign: dict) -> dict | None:
    path = out / 'recovery.json'
    if not path.exists():
        return None
    value = load(path)
    if (value['status'] != 'ready' or value['plan_sha256'] != digest(out / 'plan.json')
            or value['parent_driver_sha256'] != campaign['driver_sha256']):
        raise ValueError('recovery belongs to a different frozen plan')
    indices = value['completed_task_indices'] + value['pending_task_indices']
    if len(indices) != len(campaign['tasks']) or set(indices) != set(range(len(campaign['tasks']))):
        raise ValueError('recovery tasks do not partition the frozen plan')
    return value


def cancel_owned(out: Path) -> None:
    own = os.environ['SLURM_ARRAY_JOB_ID']
    ids = {own}
    path = out / 'jobs.json'
    if path.exists():
        jobs = load(path)
        if jobs['campaign_out'] != str(out) or jobs['plan_sha256'] != digest(out / 'plan.json'):
            raise ValueError('refusing cancellation from a different campaign')
        ids.update(jobs.get('timing', []))
        ids.update(jobs.get('models', []))
    if any(not value.isdecimal() for value in ids):
        raise ValueError('invalid owned job identifier')
    subprocess.run(['scancel', *sorted(ids)], check=False)


def guarded_benchmark(command: list[str], directory: Path, selected: dict,
                      campaign: dict, out: Path, index: int) -> None:
    process = subprocess.Popen(command, start_new_session=True)
    try:
        while True:
            peers = []
            for run in campaign['runs']:
                if run['logic'] == selected['logic'] and run['label'] != selected['label']:
                    path = out / 'timing' / run['logic'] / run['label']
                    peers.extend(journal(path / f"shard-{selected['shard']}" / 'answers.csv.jsonl', live=True))
            check_rows(journal(directory / 'answers.csv.jsonl', live=True), peers,
                       allow_execution_errors=True)
            if process.poll() is not None:
                break
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        if process.returncode != 0:
            raise RuntimeError(f'benchmark harness exited {process.returncode}')
        with (directory / 'answers.csv').open() as stream:
            check_rows(list(csv.DictReader(stream)), peers, allow_execution_errors=True)
    except BaseException as error:
        global_failure = isinstance(error, CorrectnessFailure)
        folder = 'failures' if global_failure else 'task-failures'
        write(out / folder / f'timing-{index}.json', {'error': str(error), 'selected': selected})
        for sig in (signal.SIGTERM, signal.SIGKILL):
            try:
                os.killpg(process.pid, sig)
            except ProcessLookupError:
                pass
            if sig == signal.SIGTERM:
                time.sleep(2)
        process.wait()
        if global_failure:
            cancel_owned(out)
        raise


def allocation(config: dict) -> dict:
    address_space = resource.getrlimit(resource.RLIMIT_AS)
    expected_bytes = config['memory_mib'] * 1024 * 1024
    if address_space != (expected_bytes, expected_bytes):
        raise ValueError(f'hard address-space limit missing: {address_space}')
    models = sorted({line.split(':', 1)[1].strip()
                     for line in Path('/proc/cpuinfo').read_text().splitlines()
                     if line.startswith('model name')})
    if (os.environ.get('SLURM_JOB_PARTITION') != 'compute'
            or os.environ.get('SLURM_CPUS_PER_TASK') != '1'
            or int(os.environ.get('SLURM_MEM_PER_NODE', '0')) != config['memory_mib']
            or models != ['Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz']):
        raise ValueError('wrong CPU allocation or resource limits')
    return {'job': os.environ['SLURM_JOB_ID'], 'array_job': os.environ.get('SLURM_ARRAY_JOB_ID'),
            'task': os.environ.get('SLURM_ARRAY_TASK_ID'), 'host': platform.node(),
            'partition': 'compute', 'cpu_models': models, 'memory_mib': config['memory_mib'],
            'address_space_limit_bytes': list(address_space),
            'affinity': sorted(os.sched_getaffinity(0)), 'driver_sha256': digest(Path(__file__))}


def inventory(args, config: dict, out: Path) -> None:
    logic = list(config['corpora'])[int(os.environ['SLURM_ARRAY_TASK_ID'])]
    root = (Path(os.environ['PRIMO_CORPORA_ROOT']) / logic).resolve(strict=True)
    paths = sorted(root.rglob('*.smt2'))
    inputs = {str(p.relative_to(root)): {'sha256': digest(p), 'bytes': p.stat().st_size}
              for p in paths if p.is_file()}
    if len(inputs) != config['corpora'][logic]:
        raise ValueError(f'corpus size changed: {logic}: {len(inputs)}')
    write(out / 'inventory' / f'{logic}.json', {
        **allocation(config), 'logic': logic, 'root': str(root), 'inputs': inputs,
        'count': len(inputs), 'bytes': sum(v['bytes'] for v in inputs.values()),
    })


def probe_inputs(logic: str) -> tuple[str, str]:
    if logic == 'QF_UF':
        prefix = '(declare-sort A 0)(declare-const x A)(declare-const y A)(declare-fun f (A) A)'
        sat, unsat = '(distinct x y)', '(and (= x y) (distinct (f x) (f y)))'
    elif logic in {'QF_LRA', 'QF_LIA'}:
        sort = 'Real' if logic == 'QF_LRA' else 'Int'
        prefix = f'(declare-const x {sort})'
        sat = '(= x 1)'
        unsat = '(and (> x 1) (< x 0))' if sort == 'Real' else '(= (+ x x) 1)'
    elif logic in {'QF_UFLRA', 'QF_UFLIA'}:
        sort = 'Real' if logic == 'QF_UFLRA' else 'Int'
        prefix = f'(declare-const x {sort})(declare-const y {sort})(declare-fun f ({sort}) {sort})'
        sat = '(and (= x 1) (= (f x) 2))'
        unsat = '(and (> x 0) (= x y) (distinct (f x) (f y)))'
    else:
        prefix = '(declare-sort A 0)(declare-const a A)(declare-const b A)(declare-fun f (A) A)'
        sat = '(forall ((x A)) (= x x))'
        unsat = '(and (forall ((x A)) (= (f (f x)) x)) (= (f a) b) (distinct (f b) a))'
    return tuple(f'(set-logic {logic}){prefix}(assert {formula})(check-sat)\n'
                 for formula in (sat, unsat))


def prepare(args, config: dict, out: Path) -> None:
    allocation(config)
    tools = {}
    binaries = out / 'bin'
    binaries.mkdir(exist_ok=True)
    frozen = load(args.tools_from / 'tools.json') if args.tools_from else None
    for name in ('primo', *config['references'], 'dolmen'):
        filename = 'yices-smt2' if name == 'yices' else name
        source = (Path(frozen[name]['path']) if frozen else args.workspace / (
            'build-slurm-rel/primo' if name == 'primo' else f'contrib/local/bin/{filename}'))
        if frozen and digest(source) != frozen[name]['sha256']:
            raise ValueError('frozen reference executable changed')
        target = binaries / filename
        if target.exists():
            raise ValueError('use a fresh campaign directory')
        shutil.copy2(source, target)
        result = subprocess.run([str(target), '--version'], capture_output=True, text=True,
                                timeout=15, check=True)
        tools[name] = {'path': str(target), 'sha256': digest(target),
                       'version': result.stdout + result.stderr}
    if (tools['primo']['sha256'] != config['native_binary_sha256']
            or f"git: {config['native_source_git'][:7]}" not in tools['primo']['version'].splitlines()):
        raise ValueError('unexpected native binary')
    write(out / 'tools.json', tools)
    probe(args, config, out)


def probe(args, config: dict, out: Path) -> None:
    metadata = allocation(config)
    tools = load(out / 'tools.json')
    if any(digest(Path(tool['path'])) != tool['sha256'] for tool in tools.values()):
        raise ValueError('preserved executable changed')
    capabilities = {}
    for logic in config['corpora']:
        capabilities[logic] = {}
        for name in ('primo', *config['references']):
            records = []
            for expected, text in zip(('sat', 'unsat'), probe_inputs(logic)):
                path = out / 'probes' / digest(Path(__file__))[:12] / logic / f'{expected}.smt2'
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
                command = [tools[name]['path'], *(['--quiet'] if name == 'primo' else []), str(path)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=20,
                                        check=False)
                records.append({'command': command, 'expected': expected, 'exit': result.returncode,
                                'stdout': result.stdout, 'stderr': result.stderr})
            supported = all(r['exit'] == 0 and r['stdout'].strip() == r['expected'] for r in records)
            partial_quantified = (
                logic == 'UF' and name == 'primo'
                and records[0]['exit'] == 0 and records[0]['stdout'].strip() == 'unknown'
                and records[1]['exit'] == 0 and records[1]['stdout'].strip() == 'unsat')
            diagnostic = '\n'.join(r['stdout'] + r['stderr'] for r in records).lower()
            excluded = any(word in diagnostic for word in
                           ('unsupported', 'not supported', 'unknown logic', 'unknownlogic'))
            if not supported and not partial_quantified and (name in {'primo', 'z3', 'cvc5'} or not excluded):
                write(out / 'probe-failure.json', {'logic': logic, 'solver': name, 'records': records})
                raise ValueError(f'unexpected capability probe: {logic}/{name}')
            capabilities[logic][name] = {'supported': supported or partial_quantified,
                                        'partial_sat_completion': partial_quantified,
                                        'records': records}
    current = out / 'capabilities.json'
    if current.exists():
        write(out / 'attempts' / f'capabilities-{digest(current)[:12]}.json', load(current))
    write(current, {'allocation': metadata, 'logics': capabilities})


def plan(args, config: dict, out: Path) -> None:
    capabilities = load(out / 'capabilities.json')['logics']
    tools = load(out / 'tools.json')
    tasks, runs = [], []
    for logic, expected in config['corpora'].items():
        source = out / 'inventory' / f'{logic}.json'
        corpus = load(source)
        inventory_sha = digest(source)
        if corpus['count'] != expected:
            raise ValueError('incomplete inventory')
        profiles = [('primo', config['native_profiles']['primo'])]
        if logic == 'QF_LRA':
            profiles.append(('primo-phase-mono', config['native_profiles']['primo-phase-mono']))
        profiles += [(name, []) for name in config['references'] if capabilities[logic][name]['supported']]
        shards = math.ceil(expected / config['maximum_inputs_per_shard'])
        manifests = []
        names = list(corpus['inputs'])
        for shard in range(shards):
            manifest = out / 'manifests' / logic / f'shard-{shard}.txt'
            manifest.parent.mkdir(parents=True, exist_ok=True)
            manifest.write_text(''.join(name + '\n' for name in names[shard::shards]))
            manifests.append({'manifest': str(manifest), 'manifest_sha256': digest(manifest)})
        for label, arguments in profiles:
            solver = 'primo' if label.startswith('primo') else label
            runs.append({'logic': logic, 'label': label, 'arguments': arguments,
                         'solver': solver, 'shards': shards, 'inputs': expected})
            for shard in range(shards):
                tasks.append({**runs[-1], **manifests[shard], 'shard': shard,
                              'inventory_sha256': inventory_sha})
    # Interleave theories and solvers across each scheduler chunk.
    tasks.sort(key=lambda t: (t['shard'], t['logic'], t['label']))
    model_tasks = [t for t in tasks if t['solver'] == 'primo' and t['logic'] != 'UF']
    write(out / 'plan.json', {'config': config, 'tools': tools, 'runs': runs, 'tasks': tasks,
                              'model_tasks': model_tasks,
                              'campaign_out': str(out),
                              'driver_sha256': digest(Path(__file__)),
                              'config_sha256': digest(args.config)})
    print(json.dumps({'tasks': len(tasks), 'runs': len(runs), 'corpora': config['corpora']}))


def task(args, config: dict, out: Path, phase: str) -> None:
    campaign = load(out / 'plan.json')
    resumed = recovery(out, campaign)
    if digest(args.config) != campaign['config_sha256']:
        raise ValueError('worker configuration changed after planning')
    scheduled_index = int(os.environ.get('PRIMO_TASK_OFFSET', '0')) + int(os.environ['SLURM_ARRAY_TASK_ID'])
    index = resumed['pending_task_indices'][scheduled_index] if resumed and phase == 'timing' else scheduled_index
    selected = campaign['model_tasks' if phase == 'models' else 'tasks'][index]
    tools = campaign['tools']
    source = out / 'inventory' / f"{selected['logic']}.json"
    corpus = load(source)
    tool = tools[selected['solver']]
    if digest(source) != selected['inventory_sha256'] or digest(Path(tool['path'])) != tool['sha256']:
        raise ValueError('changed inventory or executable')
    directory = out / 'timing' / selected['logic'] / selected['label'] / f"shard-{selected['shard']}"
    directory.mkdir(parents=True, exist_ok=True)
    record = {**allocation(config), 'task_index': index, 'selected': selected,
              'plan_sha256': digest(out / 'plan.json'), 'tool': tool}
    if resumed:
        record['recovery_sha256'] = digest(out / 'recovery.json')
    if phase == 'models' and (not selected['label'].startswith('primo') or selected['logic'] == 'UF'):
        return
    if phase == 'models':
        barrier = out / 'timing-audit.json'
        if (load(barrier)['status'] != 'timing-complete'
                or load(barrier)['plan_sha256'] != record['plan_sha256']):
            raise ValueError('global timing audit has not completed')
        record['parent_timing_audit_sha256'] = digest(barrier)
    write(directory / f'execution-{phase}.json', record)
    command = [sys.executable, str(args.workspace / 'scripts/benchmark_model_replay.py'),
               'benchmark' if phase == 'timing' else 'replay', '--out', str(directory)]
    if phase == 'timing':
        manifest = Path(selected['manifest'])
        if digest(manifest) != selected['manifest_sha256']:
            raise ValueError('shard manifest changed')
        command += ['--root', corpus['root'], '--manifest', str(manifest), '--solver', tool['path'],
                    '--run-id', f"{out.name}-{selected['logic']}-{selected['label']}-{selected['shard']}",
                    '--shards', '1', '--shard-index', '0',
                    '--timeout', str(config['timeout_seconds'])]
        if selected['solver'] == 'primo':
            command += ['--expected-git', config['native_source_git'][:7]]
        command += [f'--solver-arg={a}' for a in selected['arguments']]
    else:
        command += ['--model-timeout', '1800', '--dolmen-timeout', '300',
                    '--dolmen-size', '5G', '--dolmen', tools['dolmen']['path'],
                    '--dolmen-arg=--strict=false']
    expected_driver = resumed['worker_driver_sha256'] if resumed else campaign['driver_sha256']
    if digest(Path(__file__)) != expected_driver:
        raise ValueError('worker driver changed after planning')
    if phase == 'timing':
        guarded_benchmark(command, directory, selected, campaign, out, index)
    else:
        subprocess.run(command, check=True)
    if phase == 'timing':
        with (directory / 'answers.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        check_rows(rows, [], allow_execution_errors=True)
        parent = load(directory / 'provenance.json')
        expected = {name: v['sha256'] for name, v in corpus['inputs'].items()}
        if any(expected.get(name) != checksum for name, checksum in parent['inputs'].items()):
            raise ValueError('input bytes differ from inventory')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('inventory', 'prepare', 'probe', 'plan', 'timing', 'models'))
    parser.add_argument('--workspace', type=Path, default=Path(os.environ.get('PRIMO_WORKDIR', '.')))
    parser.add_argument('--out', type=Path, default=Path(os.environ.get('PRIMO_LONG_OUT', '.')))
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('config.json'))
    parser.add_argument('--tools-from', type=Path)
    args = parser.parse_args()
    config = load(args.config)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if args.phase == 'inventory':
        inventory(args, config, out)
    elif args.phase == 'prepare':
        prepare(args, config, out)
    elif args.phase == 'probe':
        probe(args, config, out)
    elif args.phase == 'plan':
        plan(args, config, out)
    else:
        task(args, config, out, args.phase)


if __name__ == '__main__':
    main()
