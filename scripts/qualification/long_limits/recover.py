#!/usr/bin/env python3
"""Preserve canceled attempts, certify complete timing shards and exercise crash isolation."""

import argparse
import csv
import os
from pathlib import Path
import shutil
import subprocess
import sys

from campaign import allocation, decisive, digest, guarded_benchmark, journal, load, recovery, write
from report import timing_shard


def prepare(out: Path) -> None:
    plan = load(out / 'plan.json')
    prior = recovery(out, plan)
    if prior and prior['worker_driver_sha256'] == digest(Path(__file__).with_name('campaign.py')):
        raise ValueError('same recovery worker already prepared; do not duplicate submission')
    jobs = load(out / 'jobs.json')
    owned = set(jobs['timing'] + jobs['models'] + [jobs['timing_audit'], jobs['final_report']])
    active = subprocess.check_output(['squeue', '--me', '--noheader', '--format=%i'], text=True)
    if owned.intersection(line.strip().split('_')[0] for line in active.splitlines()):
        raise ValueError('old campaign still has active jobs')
    complete, pending, reused, moves = [], [], {}, []
    inventories = {logic: load(out / 'inventory' / f'{logic}.json') for logic in plan['config']['corpora']}
    # Validate every candidate before moving any incomplete directory.
    for index, task in enumerate(plan['tasks']):
        root = out / 'timing' / task['logic'] / task['label'] / f"shard-{task['shard']}"
        parent = root / 'provenance.json'
        if parent.exists() and load(parent)['status'] == 'complete':
            expected = {name: value['sha256'] for name, value in inventories[task['logic']]['inputs'].items()}
            timing_shard(out, plan, task, task['shard'], expected)
            reused[f"{task['logic']}/{task['label']}/{task['shard']}"] = {
                name: digest(root / name) for name in ('answers.csv', 'provenance.json', 'execution-timing.json')}
            complete.append(index)
        else:
            pending.append(index)
            if root.exists():
                moves.append(root)
    ancestors = dict(prior.get('ancestor_recoveries', {})) if prior else {}
    archive_name = f'recovery-{len(ancestors) + 1}-canceled' if prior else 'initial-canceled'
    archive = out / 'attempts' / archive_name
    if archive.exists():
        raise ValueError('archive already exists; inspect interrupted preparation')
    archive.mkdir(parents=True)
    shutil.copy2(out / 'jobs.json', archive / 'jobs.json')
    if prior:
        ancestors[prior['worker_driver_sha256']] = digest(out / 'recovery.json')
        shutil.copy2(out / 'recovery.json', archive / 'recovery.json')
    archived = {}
    for root in moves:
        target = archive / root.relative_to(out)
        target.parent.mkdir(parents=True, exist_ok=True)
        root.rename(target)
        archived[str(root)] = str(target)
    for name in ('failures', 'task-failures', 'failed-timing-audit.json', 'failed-final-audit.json'):
        source = out / name
        if source.exists():
            source.rename(archive / name)
    write(out / 'recovery.json', {
        'status': 'ready', 'plan_sha256': digest(out / 'plan.json'),
        'parent_driver_sha256': plan['driver_sha256'],
        'worker_driver_sha256': digest(Path(__file__).with_name('campaign.py')),
        'ancestor_recoveries': ancestors,
        'completed_task_indices': complete, 'pending_task_indices': pending,
        'reused_shards': reused, 'archived_directories': archived,
        'archive': str(archive),
        'policy': 'Reference execution errors remain unsolved/error rows with original logs and PAR2 penalty. '
                  'Primo hard failures, wrong answers and decisive disagreements remain blocking.'})
    print(f'Certified {len(complete)} complete shards; {len(pending)} timing tasks remain.')


def smoke(out: Path, workspace: Path) -> None:
    plan = load(out / 'plan.json')
    config = plan['config']
    old = out / 'timing/QF_LIA/opensmt/shard-36/answers.csv.jsonl'
    original_smoke = out / 'recovery-smoke/summary.json'
    records = load(original_smoke)['rows'] if original_smoke.exists() else journal(old)
    failed = next(r for r in records if r['status'] == 'error')
    good = next(r for r in records if decisive(r))
    corpus = load(out / 'inventory/QF_LIA.json')
    tool = plan['tools']['opensmt']
    if digest(Path(tool['path'])) != tool['sha256']:
        raise ValueError('preserved reference changed')
    directory = out / 'recovery-smoke' / digest(Path(__file__).with_name('campaign.py'))[:12]
    directory.mkdir(parents=True)
    manifest = directory / 'inputs.txt'
    manifest.write_text(f"{failed['file']}\n{good['file']}\n")
    for row in (failed, good):
        path = Path(corpus['root']) / row['file']
        if digest(path) != corpus['inputs'][row['file']]['sha256']:
            raise ValueError('input changed')
    write(directory / 'execution.json', allocation(config))
    command = [sys.executable, str(workspace / 'scripts/benchmark_model_replay.py'), 'benchmark',
               '--root', corpus['root'], '--manifest', str(manifest), '--solver', tool['path'],
               '--run-id', 'long-limit-reference-crash-isolation', '--shards', '1', '--shard-index', '0',
               '--timeout', '30', '--out', str(directory)]
    os.environ.setdefault('SLURM_ARRAY_JOB_ID', os.environ['SLURM_JOB_ID'])
    guarded_benchmark(command, directory, {'logic': 'QF_LIA', 'label': 'opensmt',
                                          'solver': 'opensmt', 'shard': 36}, {'runs': []}, directory, 0)
    with (directory / 'answers.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    by_name = {row['file']: row for row in rows}
    if (len(rows) != 2 or [r['file'] for r in rows] != [failed['file'], good['file']]
            or by_name[failed['file']]['exit_code'] != '-11'
            or by_name[failed['file']]['status'] != 'error' or not decisive(by_name[good['file']])):
        raise ValueError('crash-followed-by-success integration check failed')
    write(directory / 'summary.json', {'status': 'pass', 'rows': rows,
                                      'tool': tool, 'guard_continued_after_sigsegv': True})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('prepare', 'smoke'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--workspace', type=Path, default=Path(os.environ.get('PRIMO_WORKDIR', '.')))
    args = parser.parse_args()
    if args.phase == 'prepare':
        prepare(args.out.resolve())
    else:
        smoke(args.out.resolve(), args.workspace.resolve())


if __name__ == '__main__':
    main()
