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
    for row in rows:
        if row['outcome'] not in {'pass', 'resource-exhausted'}:
            continue
        require(row['reference_exit'] == row['bounded_exit'] == 0,
                'post-answer budget replay failure')
        reference, bounded, counts = (row['reference_answers'], row['bounded_answers'],
                                      row['conflicts'])
        require(bool(reference) and len(reference) == len(bounded) == len(counts),
                'missing budget replay query')
        for expected, actual, count in zip(reference, bounded, counts):
            require(expected in {'sat', 'unsat'} and (
                (actual == 'unknown' and count == 1)
                or (actual == expected and count == 0)), 'invalid bounded answer or overshoot')
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


def verified_resource_triage(path: Path | None, candidate: str) -> dict:
    """Accept only hash-bound, clean replays with an explicit resource diagnostic."""
    if path is None:
        return {}
    records = read_json(path)
    require(records["candidate"] == candidate, 'wrong triage revision')
    result = {}
    for entry in records["cases"]:
        folder = Path(entry['replay_artifact'])
        input_hash = entry['input_sha256']
        require(input_hash not in result and digest(folder / 'input.smt2') == input_hash,
                'duplicate or changed triage input')
        require(entry['reason'] == 'integer branch-and-bound node limit',
                'unsupported resource triage reason')
        require(f'git: {candidate[:7]}' in
                (folder / 'current.version').read_text().splitlines(), 'wrong replay revision')
        for name in ('current', 'prior', 'reference'):
            require((folder / f'{name}.exit').read_text().strip() == '0',
                    'failed triage replay')
            expected = entry['reference_answer'] if name == 'reference' else 'unknown'
            require(expected in {'sat', 'unsat', 'unknown'} and
                    (folder / f'{name}.stdout').read_text().strip() == expected,
                    'triage replay answer changed')
            if name != 'reference':
                require(entry['reason'] in (folder / f'{name}.stderr').read_text().splitlines(),
                        'missing resource diagnostic')
        require(entry['reference_answer'] in {'sat', 'unsat'}, 'incomplete triage reference')
        result[input_hash] = entry
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gate-job', required=True)
    parser.add_argument('--mutation-job', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--resource-triage', type=Path)
    args = parser.parse_args()
    triage = verified_resource_triage(args.resource_triage, args.candidate)
    gate = Path('results/public-launch') / args.gate_job
    shards = [summarize_shard(gate / f'shard-{slot}', slot, args.candidate, triage)
              for slot in range(32)]
    used = [value for shard in shards for value in shard['resource_triage']]
    require(len(used) == len(set(used)) and set(used) == set(triage),
            'missing, duplicate or unexpected resource triage')
    report = aggregate(shards)
    report['resource_triage'] = list(triage.values())
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
