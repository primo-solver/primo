#!/usr/bin/env python3
"""Summarize timings after checking identities and identifying reused references."""

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
    parser.add_argument('--reference-map', type=Path,
                        help='JSON mapping logic to reference label to {root, measurement_note}. '
                             'Listed references are historical; omitted references are unavailable.')
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text())
    references = json.loads(args.reference_map.read_text()) if args.reference_map else None
    if references is not None:
        if set(references) - set(inventory['corpora']):
            raise ValueError('reference map contains an unexpected logic')
        for selected in references.values():
            if set(selected) - {'z3', 'cvc5'}:
                raise ValueError('reference map contains an unexpected solver')
            for source in selected.values():
                if not source.get('root') or not source.get('measurement_note'):
                    raise ValueError('historical reference requires root and measurement_note')
    report = {'timeout_seconds': args.timeout, 'routes': {},
              'scope': 'Fresh Primo timings; reference timing origins are explicit per run. '
                       'Model replay is a separate stage; '
                       'incomplete coverage is not independent model qualification.'}
    for logic, corpus in inventory['corpora'].items():
        raw = {}
        runs = {}
        for label in ('primo', 'z3', 'cvc5'):
            folder = args.root / logic / label
            source = None
            if label != 'primo' and references is not None:
                source = references.get(logic, {}).get(label)
                if source is None:
                    continue
                folder = Path(source['root'])
            verify_provenance(folder, corpus['inputs'], inventory['tools'][label]['sha256'],
                              ['--quiet'] if label == 'primo' else [], args.timeout)
            runs[label], raw[label] = load_run(folder, corpus['count'], args.timeout)
            if not runs[label]['timing_complete']:
                raise ValueError(f'incomplete timing: {folder}')
            runs[label]['solved_seconds'] = distribution(
                [float(row['seconds']) for row in raw[label].values() if decisive(row)])
            runs[label]['mean_par2'] = runs[label]['par2'] / corpus['count']
            runs[label]['timing_origin'] = 'historical' if source else 'fresh'
            runs[label]['timing_root'] = str(folder)
            if source:
                runs[label]['measurement_note'] = source['measurement_note']
        comparisons = {}
        for reference in ('z3', 'cvc5'):
            if reference not in raw:
                continue
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
            'unavailable_references': [label for label in ('z3', 'cvc5') if label not in raw],
            'reference_disagreements': [name for name in raw.get('z3', {})
                if name in raw.get('cvc5', {})
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
