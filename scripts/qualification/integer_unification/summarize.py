#!/usr/bin/env python3
"""Audit integer migration artifacts without executing solvers or changing timings."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from publication_followups.summarize import budget_summary
from summarize_public_launch import (
    aggregate,
    digest,
    models,
    read_csv,
    read_json,
    require,
    summarize_shard,
)


def resource_triage(paths: list[Path], candidate: str, baseline: str) -> dict:
    result = {}
    for path in paths:
        data = read_json(path)
        require(data['candidate'] == candidate, 'wrong resource replay revision')
        for row in data['cases']:
            folder = Path(row['replay_artifact'])
            checksum = row['input_sha256']
            require(checksum not in result and digest(folder / 'input.smt2') == checksum,
                    'duplicate or changed resource replay input')
            require(row['route'] == 'QF_LIA' and row['reference_answer'] in {'sat', 'unsat'},
                    'unexpected resource replay route or reference answer')
            # This migration intentionally changes the diagnostic, preserving
            # the typed resource outcome and the integer-search limit itself.
            arms = (
                ('current', candidate, 'unknown', 'integer branch-and-bound node limit'),
                ('prior', baseline, 'unknown', 'QF_LIA branch-and-bound node limit reached'),
                ('reference', None, row['reference_answer'], None),
            )
            for arm, revision, answer, reason in arms:
                require((folder / f'{arm}.exit').read_text().strip() == '0', 'replay error')
                require((folder / f'{arm}.stdout').read_text().strip() == answer,
                        'resource replay answer changed')
                if revision:
                    require(f'git: {revision[:7]}' in
                            (folder / f'{arm}.version').read_text().splitlines(),
                            'wrong resource replay binary')
                if reason:
                    require(reason in (folder / f'{arm}.stderr').read_text().splitlines(),
                            'missing resource diagnostic')
            result[checksum] = row
    return result


def mutations(folder: Path, candidate: str, binaries: dict, triage: dict,
              extra_models: Path | None) -> dict:
    require((folder / 'exit-code').read_text().strip() == '0', 'failed mutation collection')
    require(f'git: {candidate[:7]}' in (folder / 'version.txt').read_text().splitlines(),
            'wrong mutation binary')
    actual = {line.split()[1]: line.split()[0]
              for line in (folder / 'executables.sha256').read_text().splitlines()}
    require(actual == binaries, 'mixed mutation binary hashes')
    require(not any(p.is_file() for p in (folder / 'bugs').rglob('*')), 'untriaged saved bug')
    report = {}
    for stage in ('seed', 'mutant'):
        rows = read_csv(folder / f'{stage}-answers.csv')
        require(bool(rows), 'empty mutation coverage')
        paths = [Path(row['file']).resolve() for row in rows]
        require(len(paths) == len(set(paths)), 'duplicate mutation input path')
        used_triage = []
        for row, path in zip(rows, paths):
            if row['outcome'] == 'PASS':
                require(row['ours'] == row['reference'] in {'sat', 'unsat'}
                        and row['ours_exit'] == row['reference_exit'] == '0', 'invalid pass')
            else:
                require(row['outcome'] == 'INCOMPLETE', 'mutation failure')
                answers = [row[arm] for arm in ('ours', 'reference')]
                require(any(a in {'unknown', 'timeout'} for a in answers), 'missing resource')
                for arm in ('ours', 'reference'):
                    require(row[arm] in {'sat', 'unsat', 'unknown', 'timeout'} and
                            row[f'{arm}_exit'] == ('124' if row[arm] == 'timeout' else '0'),
                            'mutation process error')
                if row['ours'] == 'unknown':
                    checksum = digest(path)
                    require(checksum in triage and
                            triage[checksum]['reference_answer'] == row['reference'],
                            'untriaged mutation unknown')
                    used_triage.append(checksum)
        passed_sat = {p for r, p in zip(rows, paths)
                      if r['ours'] == 'sat' and r['outcome'] == 'PASS'}
        extra_sat = {p for r, p in zip(rows, paths)
                     if r['ours'] == 'sat' and r['outcome'] != 'PASS'}
        count = models(folder / f'{stage}-models/summary.csv', passed_sat)
        if extra_sat:
            require(extra_models is not None, 'unchecked SAT with incomplete reference')
            count += models(extra_models, extra_sat)
        hashes = [digest(p) for p in paths]
        report[stage] = {
            'count': len(rows), 'outcomes': dict(Counter(r['outcome'] for r in rows)),
            'answers': dict(Counter(r['ours'] for r in rows)), 'sat_models': count,
            'distinct_inputs': len(set(hashes)), 'resource_triage': used_triage,
            'input_manifest_sha256': hashlib.sha256(('\n'.join(hashes) + '\n').encode()).hexdigest(),
        }
    require(report['mutant']['count'] >= 100, 'insufficient mutation coverage')
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gate', type=Path, required=True)
    parser.add_argument('--mutations', type=Path, required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--resource-triage', type=Path, action='append', default=[])
    parser.add_argument('--extra-models', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    triage = resource_triage(args.resource_triage, args.candidate, args.baseline)
    shards = [summarize_shard(args.gate / f'shard-{i}', i, args.candidate, triage)
              for i in range(32)]
    report = aggregate(shards)
    for shard in shards:
        hashes = shard.pop('input_sha256')
        shard['input_manifest_sha256'] = hashlib.sha256(
            ('\n'.join(hashes) + '\n').encode()).hexdigest()
        shard['budget_replay'] = budget_summary(
            args.gate / f"shard-{shard['slot']}" / 'budget-replay')
    report['shards'] = shards
    report['mutations'] = [mutations(args.mutations / f'shard-{i}', args.candidate,
                                    shards[0]['binaries'], triage, args.extra_models)
                           for i in range(2)]
    used = [h for s in shards for h in s['resource_triage']]
    used += [h for m in report['mutations'] for stage in m.values()
             for h in stage['resource_triage']]
    require(len(used) == len(set(used)) and set(used) == set(triage),
            'unused, duplicate or missing resource triage')
    report['resource_triage'] = triage
    report['command'] = sys.argv
    report['audit_script_sha256'] = digest(Path(__file__))
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(args.out)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
