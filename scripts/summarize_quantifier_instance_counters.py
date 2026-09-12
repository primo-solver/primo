#!/usr/bin/env python3
"""Compare quantifier instance counters between arms of a UF comparison.

Reads the per-solver `shard-N.csv` files that slurm_uf_quantifier_compare.sbatch
writes (one subdirectory per arm, run with `--stats`). For each arm it reports
solved counts and the sums of the `:quant.*` instance counters over the rows
that carry statistics, which include timeouts when Primo printed statistics on
termination. For a pair of arms it lists the files only one of them solves,
and per family how many each solves.

A file counts as solved only with status `ok` and answer `sat` or `unsat`.
Correctness against references is checked by summarize_uf_quantifier_compare.py.

Usage:
    python3 scripts/summarize_quantifier_instance_counters.py RESULT_DIR \
        --arms primo primo-nosimp [--json OUT.json]
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path

COUNTERS = (
    ":quant.instances",
    ":quant.instances.raw-duplicates",
    ":quant.instances.simplified",
    ":quant.instances.simplified-duplicates",
    ":quant.instances.trivial",
    ":quant.instances.duplicate-lemmas",
    ":quant.lemmas",
    ":quant.rounds",
    ":quant.pool-terms",
)


def solved(row: dict[str, str]) -> bool:
    return row["status"] == "ok" and row["answer"] in ("sat", "unsat")


def load(directory: Path) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for shard in sorted(directory.glob("shard-*[0-9].csv")):
        with shard.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                rows[row["file"]] = row
    return rows


def as_int(value: str | None) -> int:
    try:
        return int(value) if value not in (None, "") else 0
    except ValueError:
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", type=Path)
    parser.add_argument("--arms", nargs=2, required=True, metavar=("FIRST", "SECOND"))
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    arms = {name: load(args.directory / name) for name in args.arms}
    summary: dict[str, object] = {"directory": str(args.directory), "arms": {}}
    for name, rows in arms.items():
        with_stats = [row for row in rows.values() if row.get(":quant.checks", "") != ""]
        totals = {counter: sum(as_int(row.get(counter)) for row in with_stats) for counter in COUNTERS}
        outcomes = Counter(row["answer"] if row["status"] == "ok" else row["status"] for row in rows.values())
        summary["arms"][name] = {
            "files": len(rows),
            "solved": sum(solved(row) for row in rows.values()),
            "outcomes": dict(outcomes),
            "rows_with_statistics": len(with_stats),
            "counters": totals,
        }

    first, second = args.arms
    common = sorted(set(arms[first]) & set(arms[second]))
    only_first = [f for f in common if solved(arms[first][f]) and not solved(arms[second][f])]
    only_second = [f for f in common if solved(arms[second][f]) and not solved(arms[first][f])]
    families: dict[str, Counter[str]] = defaultdict(Counter)
    for f in common:
        family = arms[first][f]["family"]
        families[family]["files"] += 1
        families[family][first] += solved(arms[first][f])
        families[family][second] += solved(arms[second][f])
    summary.update(
        {
            "common_files": len(common),
            f"solved_only_by_{first}": only_first,
            f"solved_only_by_{second}": only_second,
            "families": {name: dict(counts) for name, counts in sorted(families.items())},
        }
    )
    if args.json:
        args.json.write_text(json.dumps(summary, indent=1, sort_keys=True) + "\n", encoding="utf-8")

    for name in args.arms:
        arm = summary["arms"][name]
        print(f"{name}: files {arm['files']}, solved {arm['solved']}, outcomes {arm['outcomes']}")
        print(f"  rows with statistics: {arm['rows_with_statistics']}")
        for counter, value in arm["counters"].items():
            print(f"  {counter} {value}")
    print(f"common files {len(common)}; solved only by {first}: {len(only_first)}; "
          f"solved only by {second}: {len(only_second)}")
    for f in only_first[:10]:
        print(f"  only {first}: {f}")
    for f in only_second[:10]:
        print(f"  only {second}: {f}")
    print(f"family | files {first} {second}")
    for name, counts in summary["families"].items():
        print(f"  {name} | {counts.get('files', 0)} {counts.get(first, 0)} {counts.get(second, 0)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
