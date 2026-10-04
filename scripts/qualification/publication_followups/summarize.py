#!/usr/bin/env python3
"""Check publication-followup artifacts without running any solver.

Run from the SLURM experiment workspace, where recorded input paths resolve.
The existing eight-route verifier checks answers and independent SAT models;
this adds exact budget-replay coverage and seed/mutant model joins.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from summarize_public_launch import (
    aggregate,
    digest,
    models,
    read_csv,
    read_json,
    require,
    summarize_shard,
)


def budget_summary(folder: Path) -> dict:
    rows = [read_json(p) for p in sorted(folder.glob('*/result.json'))]
    require(len(rows) == 25, 'missing or duplicate budget replay rows')
    outcomes = Counter(row['outcome'] for row in rows)
    require(set(outcomes) <= {
        'pass', 'resource-exhausted', 'timeout', 'reference-incomplete',
    }, 'failed budget replay')
    for index, row in enumerate(rows):
        require(digest(folder / f'{index:05d}/input.smt2') == row['adapted_sha256'],
                'budget replay input changed')
        require(digest(Path(row['input'])) == row['sha256'], 'original input changed')
    exhausted = sum(row['exhausted_queries'] for row in rows)
    summary = read_json(folder / 'summary.json')
    require(summary['count'] == len(rows) and summary['outcomes'] == dict(outcomes)
            and summary['exhausted_queries'] == exhausted, 'budget summary mismatch')
    summary['queries'] = sum(len(row['bounded_answers']) for row in rows)
    summary['conflict_counts'] = dict(Counter(
        str(count) for row in rows for count in row['conflicts']))
    return summary


def mutation_summary(folder: Path, candidate: str, binaries: dict) -> dict:
    require((folder / 'exit-code').read_text().strip() == '0', 'failed mutation shard')
    require(f'git: {candidate[:7]}' in (folder / 'version.txt').read_text().splitlines(),
            'wrong mutation revision')
    actual = {line.split()[1]: line.split()[0]
              for line in (folder / 'executables.sha256').read_text().splitlines()}
    require(actual == binaries, 'mixed mutation binaries')
    require(not any(p.is_file() for p in (folder / 'bugs').rglob('*')),
            'untriaged mutation failure')
    report = {}
    for stage in ('seed', 'mutant'):
        rows = read_csv(folder / f'{stage}-answers.csv')
        require(bool(rows), 'empty mutation stage')
        paths = [Path(row['file']).resolve() for row in rows]
        require(len(set(paths)) == len(paths), 'duplicate measured mutation input')
        for row in rows:
            require(row['outcome'] == 'PASS' and row['ours_exit'] == '0'
                    and row['reference_exit'] == '0'
                    and row['ours'] == row['reference'] in {'sat', 'unsat'},
                    'incomplete or failing mutation replay')
        sat = {path for path, row in zip(paths, rows) if row['ours'] == 'sat'}
        hashes = [digest(path) for path in paths]
        report[stage] = {
            'count': len(rows), 'pass': len(rows),
            'sat_models': models(folder / f'{stage}-models/summary.csv', sat),
            'distinct_inputs': len(set(hashes)),
            'input_manifest_sha256': hashlib.sha256(
                ('\n'.join(sorted(hashes)) + '\n').encode()).hexdigest(),
        }
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gate-job', required=True)
    parser.add_argument('--mutation-job', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    gate = Path('results/public-launch') / args.gate_job
    shards = [summarize_shard(gate / f'shard-{slot}', slot, args.candidate)
              for slot in range(32)]
    report = aggregate(shards)
    for shard in shards:
        hashes = shard.pop('input_sha256')
        shard['distinct_inputs'] = len(set(hashes))
        shard['input_manifest_sha256'] = hashlib.sha256(
            ('\n'.join(sorted(hashes)) + '\n').encode()).hexdigest()
        shard['budget_replay'] = budget_summary(
            gate / f"shard-{shard['slot']}" / 'budget-replay')
    mutations = Path('results/public-mutations') / args.mutation_job
    report['mutations'] = [mutation_summary(mutations / f'shard-{slot}', args.candidate,
                                            shards[0]['binaries']) for slot in range(2)]
    require(sum(row['mutant']['count'] for row in report['mutations']) >= 100,
            'fewer than 100 real-seed mutations')
    report['candidate'] = args.candidate
    report['jobs'] = {'generated': args.gate_job, 'mutations': args.mutation_job}
    report['scope'] = ('Correctness samples; resource exhaustion and timeouts are separate '
                       'from successful solving. No performance claim.')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['routes'], indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
