#!/usr/bin/env python3
"""Build a family-complete mutation-fuzz seed corpus from a benchmark root.

Why this exists. The mutation fuzzers (opfuzz, YinYang typefuzz) mutate a
directory of seed formulas, so whatever is *not* in that directory is never
fuzzed. On 2026-08-01 the QF_LRA seed corpus was found to hold 14 files
drawn from 3 of the division's 17 families -- and none from the families
that stress the tableau hardest: `latendresse` (pure LP, zero decisions,
enormous pivot counts) and `LassoRanker` (the division's second-largest
family). Items H8, H9 and H10 all rewrote the pivot hot path, and none of
their mutation fuzzing ever saw a formula shaped like those.

Seeds are committed, so the corpus is kept small on purpose. This script
takes the *smallest* files per family instead, which keeps mutation cheap
while guaranteeing every family is represented -- so the corpus can be
regenerated on a machine that has the benchmarks rather than stored in the
repository.

Selection is deterministic: smallest first, ties broken by path, so the same
root always yields the same corpus.

Example:
    python3 scripts/build_fuzz_seed_corpus.py --root tmp/QF_LRA \\
        --out /tmp/qf_lra_family_seeds --per-family 2 --max-bytes 200000
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def build(root: Path, out: Path, per_family: int, max_bytes: int) -> list[tuple[str, int, int]]:
    out.mkdir(parents=True, exist_ok=True)
    report: list[tuple[str, int, int]] = []
    for family_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        candidates = sorted(
            (p for p in family_dir.rglob("*.smt2")),
            key=lambda p: (p.stat().st_size, str(p)),
        )
        # A family whose smallest file exceeds the cap still gets its single
        # smallest one. Skipping it would recreate exactly the blind spot
        # this script exists to remove -- `latendresse`'s smallest is 122KB
        # and it is the most important family here to fuzz.
        eligible = [p for p in candidates if p.stat().st_size <= max_bytes] or candidates[:1]
        chosen = eligible[:per_family]
        for path in chosen:
            shutil.copy2(path, out / f"{family_dir.name}_{path.name}")
        if chosen:
            report.append((family_dir.name, len(chosen), sum(p.stat().st_size for p in chosen)))
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, required=True, help="benchmark root, one directory per family")
    parser.add_argument("--out", type=Path, required=True, help="seed directory to create")
    parser.add_argument("--per-family", type=int, default=2)
    parser.add_argument("--max-bytes", type=int, default=200_000)
    args = parser.parse_args()

    if not args.root.is_dir():
        raise SystemExit(f"benchmark root not found: {args.root}")

    report = build(args.root, args.out, args.per_family, args.max_bytes)
    total_files = sum(count for _, count, _ in report)
    total_bytes = sum(size for _, _, size in report)
    for family, count, size in report:
        print(f"  {family:<46} {count:>2} file(s)  {size / 1024:>9.1f} KB")
    print(f"{len(report)} families, {total_files} seeds, {total_bytes / 1024:.1f} KB -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
