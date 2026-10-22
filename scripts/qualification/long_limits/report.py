#!/usr/bin/env python3
"""Strict all-theory gate/timing/model audits and best-static-profile cactus plots."""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import os
from pathlib import Path
import statistics
import subprocess
import sys

from campaign import check_rows, decisive, digest, load, write


def gate(args, plan_config: dict) -> None:
    sys.path.insert(0, str(args.workspace / 'scripts/qualification'))
    from summarize_public_launch import summarize_shard

    tools = load(args.out / 'tools.json')
    root = args.workspace / 'results/public-launch' / os.environ['PRIMO_GATE_JOB']
    shards = [summarize_shard(root / f'shard-{i}', i, plan_config['native_source_git'])
              for i in range(24)]
    expected = {'build-slurm-rel/primo': tools['primo']['sha256'],
                'contrib/local/bin/z3': tools['z3']['sha256'],
                'contrib/local/bin/dolmen': tools['dolmen']['sha256']}
    if any(s['binaries'] != expected for s in shards):
        raise ValueError('gate used different executables')
    report = {'status': 'complete', 'routes': {}, 'shards': shards}
    for logic in plan_config['corpora']:
        selected = [s for s in shards if s['route'] == logic]
        report['routes'][logic] = {key: sum(s[key] for s in selected)
                                   for key in ('cases', 'pass', 'timeout', 'sat_models', 'resource_incomplete')}
        for s in selected:
            hashes = s.pop('input_sha256')
            s['distinct_inputs'] = len(set(hashes))
            s['input_hashes_sha256'] = hashlib.sha256(
                ('\n'.join(sorted(hashes)) + '\n').encode()).hexdigest()
    write(args.out / 'gate-audit.json', report)


def metrics(rows: list[dict], timeout: int) -> dict:
    solved = [r for r in rows if decisive(r)]
    times = sorted(float(r['seconds']) for r in solved)
    return {'inputs': len(rows), 'solved': len(solved),
            'sat': sum(r['answer'] == 'sat' for r in solved),
            'unsat': sum(r['answer'] == 'unsat' for r in solved),
            'timeouts': sum(r['status'] == 'timeout' for r in rows),
            'unknown': sum(r['status'] == 'ok' and r['answer'] == 'unknown' for r in rows),
            'errors': sum(r['status'] not in {'ok', 'timeout'} for r in rows),
            'par2_sum': sum(times) + 2 * timeout * (len(rows) - len(solved)),
            'median_solved_seconds': statistics.median(times) if times else None}


def collect(args, final: bool) -> dict:
    plan = load(args.out / 'plan.json')
    if load(args.out / 'gate-audit.json')['status'] != 'complete':
        raise ValueError('fresh correctness gate incomplete')
    sys.path.insert(0, str(args.workspace / 'scripts/qualification/lra_ablation'))
    from summarize import audit_models

    all_rows, reports = {}, {}
    for run in plan['runs']:
        logic, label = run['logic'], run['label']
        inventory_path = args.out / 'inventory' / f'{logic}.json'
        inventory = load(inventory_path)
        expected = {name: value['sha256'] for name, value in inventory['inputs'].items()}
        merged, model_count = {}, 0
        for index in range(run['shards']):
            root = args.out / 'timing' / logic / label / f'shard-{index}'
            parent = load(root / 'provenance.json')
            execution = load(root / 'execution-timing.json')
            selected_names = list(expected)[index::run['shards']]
            if (parent['status'] != 'complete' or not parent['inputs_unchanged']
                    or parent['inputs'] != {name: expected[name] for name in selected_names}
                    or parent['solver']['sha256'] != plan['tools'][run['solver']]['sha256']
                    or parent['solver_args'] != run['arguments']
                    or parent['timeout'] != plan['config']['timeout_seconds']
                    or parent['shards'] != 1 or parent['shard_index'] != 0
                    or execution['selected']['shard'] != index
                    or execution['selected']['shards'] != run['shards']
                    or execution['plan_sha256'] != digest(args.out / 'plan.json')
                    or execution['driver_sha256'] != plan['driver_sha256']
                    or execution['memory_mib'] != plan['config']['memory_mib']
                    or execution['partition'] != 'compute'
                    or execution['cpu_models'] != ['Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz']):
                raise ValueError(f'invalid timing provenance: {root}')
            with (root / 'answers.csv').open() as stream:
                rows = list(csv.DictReader(stream))
            names = [row['file'] for row in rows]
            if (len(set(names)) != len(names) or set(names) != set(selected_names)
                    or set(merged) & set(names)):
                raise ValueError(f'coverage mismatch: {root}')
            check_rows(rows, [])
            merged.update((r['file'], r) for r in rows)
            if final and run['solver'] == 'primo' and logic != 'UF':
                model_execution = load(root / 'execution-models.json')
                if (model_execution['parent_timing_audit_sha256'] != digest(args.out / 'timing-audit.json')
                        or model_execution['plan_sha256'] != digest(args.out / 'plan.json')):
                    raise ValueError('models lack the complete global timing barrier')
                model_count += audit_models(root, parent, rows)
        if set(merged) != set(expected):
            raise ValueError('incomplete division')
        values = list(merged.values())
        result = metrics(values, plan['config']['timeout_seconds'])
        result['arguments'] = run['arguments']
        result['direct_dolmen_models'] = model_count if final and run['solver'] == 'primo' and logic != 'UF' else None
        result['families'] = {
            family: metrics([r for r in values if r['family'] == family], plan['config']['timeout_seconds'])
            for family in sorted({r['family'] for r in values})}
        reports.setdefault(logic, {})[label] = result
        all_rows.setdefault(logic, {})[label] = merged
        directory = args.out / 'plot-data' / logic / label
        directory.mkdir(parents=True, exist_ok=True)
        with (directory / 'shard-0.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=sorted({k for r in values for k in r}))
            writer.writeheader()
            writer.writerows(values)
    for logic, runs in all_rows.items():
        for first, second in itertools.combinations(runs, 2):
            check_rows(list(runs[first].values()), list(runs[second].values()))
    best = {logic: min((label for label in runs if label.startswith('primo')),
                      key=lambda label: (-runs[label]['solved'], runs[label]['par2_sum'], label))
            for logic, runs in reports.items()}
    corroboration = {}
    for logic, runs in all_rows.items():
        for label, values in runs.items():
            if not label.startswith('primo'):
                continue
            refs = [rows for name, rows in runs.items() if not name.startswith('primo')]
            missing = [name for name, row in values.items() if decisive(row)
                       and not any(decisive(reference[name]) and reference[name]['answer'] == row['answer']
                                   for reference in refs)]
            corroboration[f'{logic}/{label}'] = {'uncorroborated_files': missing,
                                               'uncorroborated_count': len(missing)}
    report = {'status': 'complete-with-quantified-model-limitation' if final else 'timing-complete',
              'plan_sha256': digest(args.out / 'plan.json'), 'runs': reports, 'best_static_profiles': best,
              'selection_rule': 'One profile per entire division: greatest solved, then lowest PAR2, then name.',
              'reference_corroboration': corroboration,
              'quantified_models': 'UF printed models unsupported; no independent model qualification claimed.',
              'ground_models_complete': final, 'timeout_seconds': plan['config']['timeout_seconds']}
    if final:
        plots = args.out / 'plots'
        for logic, runs in reports.items():
            selected = [best[logic], *(name for name in runs if not name.startswith('primo'))]
            command = [sys.executable, str(args.workspace / 'scripts/plot_cactus.py')]
            for name in selected:
                display = {'primo': 'Primo default', 'primo-phase-mono': 'Primo phase+mono',
                           'z3': 'Z3', 'cvc5': 'cvc5', 'yices': 'Yices', 'opensmt': 'OpenSMT'}[name]
                command += ['--series', f'{display}={args.out / "plot-data" / logic / name}']
            highlight = 'Primo phase+mono' if best[logic] == 'primo-phase-mono' else 'Primo default'
            command += ['--timeout', '1200', '--title', f'{logic}: full corpus, 1200 seconds',
                        '--highlight-series', highlight, '--sort-by-solved',
                        '--note', 'Fresh matched CPU compute runs; one physical CPU, 8 GiB; Primo 4419dc9.',
                        '--note', ('Quantified UF: no printed-model qualification; see reference corroboration.'
                                   if logic == 'UF' else 'All successful Primo SAT rows separately validated by Dolmen.'),
                        '--note', 'No easy-input filtering. Times below 0.01s share the left edge.',
                        '--out', str(plots / f'{logic.lower()}-1200.svg')]
            subprocess.run(command, check=True)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('gate', 'timing', 'final'))
    parser.add_argument('--workspace', type=Path, default=Path(os.environ.get('PRIMO_WORKDIR', '.')))
    parser.add_argument('--out', type=Path, default=Path(os.environ.get('PRIMO_LONG_OUT', '.')))
    args = parser.parse_args()
    args.out = args.out.resolve()
    try:
        if args.stage == 'gate':
            gate(args, load(Path(__file__).with_name('config.json')))
        else:
            write(args.out / ('qualified-audit.json' if args.stage == 'final' else 'timing-audit.json'),
                  collect(args, args.stage == 'final'))
    except Exception as error:
        write(args.out / f'failed-{args.stage}-audit.json', {'error': str(error), 'status': 'incomplete-or-failed'})
        raise


if __name__ == '__main__':
    main()
