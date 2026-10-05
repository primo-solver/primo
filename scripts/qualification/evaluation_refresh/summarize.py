#!/usr/bin/env python3
"""Summarize fresh timings only after checking their corpus and binary identities."""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from summarize_model_campaign import decisive, load_run


def distribution(values: list[float]) -> dict:
    ordered = sorted(values)
    return {
        'count': len(ordered),
        'median': statistics.median(ordered) if ordered else None,
        'p95': ordered[math.ceil(.95 * len(ordered)) - 1] if ordered else None,
        'maximum': max(ordered, default=None),
    }


def verify_provenance(root: Path, expected: dict, binary_hash: str,
                      arguments: list[str], timeout: float) -> None:
    observed = {}
    for path in sorted(root.glob('shard-*/provenance.json')):
        row = json.loads(path.read_text())
        if (row['solver']['sha256'] != binary_hash or row['solver_args'] != arguments
                or row['timeout'] != timeout):
            raise ValueError(f'mismatched timing profile: {path}')
        if set(observed) & set(row['inputs']):
            raise ValueError(f'duplicate timing inputs: {path}')
        observed.update(row['inputs'])
    if observed != {name: entry['sha256'] for name, entry in expected.items()}:
        raise ValueError(f'missing, unexpected or changed timing inputs: {root}')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text())
    report = {'timeout_seconds': args.timeout, 'routes': {},
              'scope': 'Fresh timing baseline. Model replay is a separate stage; '
                       'incomplete coverage is not independent model qualification.'}
    for logic, corpus in inventory['corpora'].items():
        raw = {}
        runs = {}
        for label in ('primo', 'z3', 'cvc5'):
            folder = args.root / logic / label
            verify_provenance(folder, corpus['inputs'], inventory['tools'][label]['sha256'],
                              ['--quiet'] if label == 'primo' else [], args.timeout)
            runs[label], raw[label] = load_run(folder, corpus['count'], args.timeout)
            if not runs[label]['timing_complete']:
                raise ValueError(f'incomplete timing: {folder}')
            runs[label]['solved_seconds'] = distribution(
                [float(row['seconds']) for row in raw[label].values() if decisive(row)])
            runs[label]['mean_par2'] = runs[label]['par2'] / corpus['count']
        comparisons = {}
        for reference in ('z3', 'cvc5'):
            paired = [name for name in raw['primo'] if decisive(raw['primo'][name])
                      and decisive(raw[reference][name])]
            ratios = [float(raw['primo'][name]['seconds']) /
                      max(float(raw[reference][name]['seconds']), 1e-6) for name in paired]
            comparisons[reference] = {
                'jointly_solved': len(paired),
                'disagreements': [name for name in paired if
                                  raw['primo'][name]['answer'] != raw[reference][name]['answer']],
                'primo_over_reference_seconds': distribution(ratios),
                'geometric_mean_ratio': math.exp(statistics.mean(math.log(max(r, 1e-6))
                                               for r in ratios)) if ratios else None,
            }
        report['routes'][logic] = {
            'runs': runs, 'comparisons': comparisons,
            'reference_disagreements': [name for name in raw['z3']
                if decisive(raw['z3'][name]) and decisive(raw['cvc5'][name])
                and raw['z3'][name]['answer'] != raw['cvc5'][name]['answer']],
        }
    report['answer_failures'] = {
        logic: {label: run['exception_rows'] for label, run in route['runs'].items()
                if run['exception_rows']}
        for logic, route in report['routes'].items()
        if any(run['exception_rows'] for run in route['runs'].values())}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    return int(bool(report['answer_failures']) or any(
        route['reference_disagreements'] for route in report['routes'].values()) or any(
        pair['disagreements'] for route in report['routes'].values()
        for pair in route['comparisons'].values()))


if __name__ == '__main__':
    raise SystemExit(main())
