#!/usr/bin/env python3
"""Run scripts/analyze_smt2.py over a whole benchmark corpus, aggregated by family.

Written to settle the "monotone (one-sided) variable elimination" lead on the
full QF_LRA corpus rather than on one file per family. Kept because the same
question -- "does structural property X hold corpus-wide, or did we conclude
it from a handful of files?" -- keeps coming up.

Failures matter as much as results: a file that timed out, ran out of memory,
or failed to parse is **not** evidence that it has no one-sided variables. Such
files are counted in their own columns and excluded from the ratios, never
silently folded in as zeros.

Usage:
  python3 scripts/screen_qf_lra_corpus.py ROOT --out results/dir [--jobs N]
          [--timeout SEC] [--mem-mb MB]
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import os
import resource
import subprocess
import sys
import time
from concurrent import futures

ANALYZER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'analyze_smt2.py')


def _limit(mem_mb):
    def apply():
        soft = mem_mb * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (soft, soft))
    return apply


def run_one(path, timeout, mem_mb):
    """Analyze one file in a subprocess so a hang or OOM cannot take the run down."""
    started = time.time()
    try:
        proc = subprocess.run(
            [sys.executable, ANALYZER, '--json', path],
            capture_output=True, text=True, timeout=timeout,
            preexec_fn=_limit(mem_mb), check=False)
    except subprocess.TimeoutExpired:
        return {'file': path, 'status': 'timeout', 'seconds': timeout}
    except Exception as exc:                      # noqa: BLE001 - report, never drop
        return {'file': path, 'status': f'spawn-error:{type(exc).__name__}'}
    elapsed = round(time.time() - started, 2)
    if proc.returncode != 0:
        tail = (proc.stderr or '').strip().splitlines()
        reason = tail[-1][:120] if tail else f'exit{proc.returncode}'
        return {'file': path, 'status': 'error', 'seconds': elapsed, 'detail': reason}
    try:
        info = json.loads(proc.stdout)[0]
    except Exception:                             # noqa: BLE001
        return {'file': path, 'status': 'bad-json', 'seconds': elapsed}
    info['status'] = 'ok'
    info['seconds'] = elapsed
    return info


def family_of(path, root):
    rel = os.path.relpath(path, root)
    return rel.split(os.sep)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('root')
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--mem-mb', type=int, default=4096)
    args = ap.parse_args()

    files = []
    for dirpath, _, names in os.walk(args.root):
        for name in names:
            if name.endswith('.smt2'):
                files.append(os.path.join(dirpath, name))
    files.sort()
    os.makedirs(args.out, exist_ok=True)
    rows_path = os.path.join(args.out, 'per_file.csv')
    print(f'{len(files)} files, {args.jobs} workers, '
          f'{args.timeout}s timeout, {args.mem_mb}MB cap', flush=True)

    fields = ['file', 'family', 'status', 'seconds', 'reals', 'bools',
              'reals_one_sided', 'reals_two_sided', 'reals_unconstrained',
              'reals_fully_guard_protected', 'distinct_atoms', 'opaque_atoms',
              'atoms_pure_positive', 'atoms_pure_negative', 'atoms_mixed_polarity',
              'detail']
    done = 0
    with open(rows_path, 'w', newline='') as fh:
        writer = csv.DictWriter(fh, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        with futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futs = {pool.submit(run_one, f, args.timeout, args.mem_mb): f for f in files}
            for fut in futures.as_completed(futs):
                row = fut.result()
                row['family'] = family_of(row['file'], args.root)
                writer.writerow(row)
                fh.flush()
                done += 1
                if done % 100 == 0:
                    print(f'  {done}/{len(files)}', flush=True)

    # ---- aggregate
    agg = collections.defaultdict(lambda: collections.Counter())
    with open(rows_path) as fh:
        for row in csv.DictReader(fh):
            fam = agg[row['family']]
            fam['files'] += 1
            if row['status'] != 'ok':
                fam['failed'] += 1
                fam[row['status']] += 1
                continue
            fam['analyzed'] += 1
            for key in ('reals', 'reals_one_sided', 'reals_two_sided',
                        'reals_unconstrained', 'reals_fully_guard_protected',
                        'distinct_atoms', 'opaque_atoms'):
                fam[key] += int(row[key] or 0)
            if int(row['opaque_atoms'] or 0) > 0:
                fam['files_with_opaque'] += 1

    summary_path = os.path.join(args.out, 'summary.txt')
    with open(summary_path, 'w') as out:
        header = (f"{'family':<42}{'files':>6}{'ok':>6}{'fail':>5}"
                  f"{'reals':>9}{'1-sided':>9}{'2-sided':>9}{'unconstr':>9}"
                  f"{'opaque%':>9}{'files_op':>9}")
        lines = [header, '-' * len(header)]
        tot = collections.Counter()
        for fam in sorted(agg):
            c = agg[fam]
            atoms = c['distinct_atoms'] or 1
            lines.append(
                f"{fam:<42}{c['files']:>6}{c['analyzed']:>6}{c['failed']:>5}"
                f"{c['reals']:>9}{c['reals_one_sided']:>9}{c['reals_two_sided']:>9}"
                f"{c['reals_unconstrained']:>9}"
                f"{100.0 * c['opaque_atoms'] / atoms:>8.1f}%{c['files_with_opaque']:>9}")
            tot.update(c)
        atoms = tot['distinct_atoms'] or 1
        lines.append('-' * len(header))
        lines.append(
            f"{'TOTAL':<42}{tot['files']:>6}{tot['analyzed']:>6}{tot['failed']:>5}"
            f"{tot['reals']:>9}{tot['reals_one_sided']:>9}{tot['reals_two_sided']:>9}"
            f"{tot['reals_unconstrained']:>9}"
            f"{100.0 * tot['opaque_atoms'] / atoms:>8.1f}%{tot['files_with_opaque']:>9}")
        text = '\n'.join(lines)
        out.write(text + '\n')
    print(text)
    print(f'\nper-file rows: {rows_path}\nsummary: {summary_path}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
