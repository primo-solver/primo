#!/usr/bin/env python3
"""Summarize slurm_uf_quantifier_compare.sbatch results.

The results directory holds one subdirectory per solver, each with the
`shard-N.csv` files written by bench_qf_uf_full.py. A shard killed by the
scheduler writes no CSV, so coverage is checked first: the number of shard
files and the number of distinct rows per solver are reported next to the
expected counts, and a short run is flagged rather than read as clean.

A file counts as solved only with process status `ok` and answer `sat` or
`unsat`. The candidate (default `primo`) is wrong on a file when its definite
answer contradicts the declared `:status`, and it disagrees with a reference
when that reference gave the opposite definite answer. Both are listed by file.
Exit status is 1 when the candidate has a wrong answer or a disagreement, or
when coverage is incomplete.

Usage:
    python3 scripts/summarize_uf_quantifier_compare.py RESULT_DIR \
        --shards 80 --expected-files 7590 [--json OUT.json]
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path

DEFINITE = {"sat", "unsat"}


def outcome(row: dict[str, str]) -> str:
    if row["status"] != "ok":
        return row["status"]
    return row["answer"]


def load_solver(directory: Path) -> tuple[dict[str, dict[str, str]], int]:
    rows: dict[str, dict[str, str]] = {}
    shard_files = sorted(directory.glob("shard-*[0-9].csv"))
    for shard in shard_files:
        with shard.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                rows[row["file"]] = row
    return rows, len(shard_files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", type=Path)
    parser.add_argument("--candidate", default="primo")
    parser.add_argument("--shards", type=int, required=True)
    parser.add_argument("--expected-files", type=int, required=True)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    solvers: dict[str, dict[str, dict[str, str]]] = {}
    coverage: dict[str, dict[str, int]] = {}
    for directory in sorted(path for path in args.directory.iterdir() if path.is_dir()):
        rows, shard_count = load_solver(directory)
        if not rows and shard_count == 0:
            continue
        solvers[directory.name] = rows
        coverage[directory.name] = {"shards": shard_count, "rows": len(rows)}
    if args.candidate not in solvers:
        raise SystemExit(f"no results for candidate '{args.candidate}' in {args.directory}")
    references = [name for name in solvers if name != args.candidate]

    complete = all(
        item["shards"] == args.shards and item["rows"] == args.expected_files
        for item in coverage.values()
    )
    outcomes = {name: Counter(outcome(row) for row in rows.values()) for name, rows in solvers.items()}

    candidate = solvers[args.candidate]
    wrong_status: list[dict[str, str]] = []
    disagreements: list[dict[str, str]] = []
    reference_conflicts: list[dict[str, str]] = []
    only_candidate: list[str] = []
    families: dict[str, Counter[str]] = defaultdict(Counter)
    for file, row in sorted(candidate.items()):
        answer = outcome(row)
        families[row["family"]][answer] += 1
        expected = row["expected"]
        if answer in DEFINITE and expected in DEFINITE and answer != expected:
            wrong_status.append({"file": file, "answer": answer, "expected": expected})
        reference_answers = {
            name: outcome(solvers[name][file]) for name in references if file in solvers[name]
        }
        for name, other in reference_answers.items():
            if answer in DEFINITE and other in DEFINITE and answer != other:
                disagreements.append({"file": file, "candidate": answer, name: other})
        definite = {name: other for name, other in reference_answers.items() if other in DEFINITE}
        if len(set(definite.values())) > 1:
            reference_conflicts.append({"file": file, **definite})
        if answer in DEFINITE and not definite:
            only_candidate.append(file)

    summary = {
        "directory": str(args.directory),
        "candidate": args.candidate,
        "coverage": coverage,
        "coverage_complete": complete,
        "outcomes": {name: dict(counts) for name, counts in outcomes.items()},
        "solved": {
            name: sum(counts[answer] for answer in DEFINITE) for name, counts in outcomes.items()
        },
        "candidate_wrong_vs_status": wrong_status,
        "candidate_disagreements": disagreements,
        "reference_conflicts": reference_conflicts,
        "candidate_only_solved": only_candidate,
        "candidate_families": {family: dict(counts) for family, counts in sorted(families.items())},
    }
    if args.json:
        args.json.write_text(json.dumps(summary, indent=1, sort_keys=True) + "\n", encoding="utf-8")

    print(f"coverage (expected {args.shards} shards, {args.expected_files} files): "
          f"{'complete' if complete else 'INCOMPLETE'}")
    for name, item in coverage.items():
        print(f"  {name}: {item['shards']} shards, {item['rows']} files")
    for name, counts in outcomes.items():
        detail = ", ".join(f"{key} {value}" for key, value in sorted(counts.items()))
        print(f"{name}: solved {summary['solved'][name]} ({detail})")
    print(f"{args.candidate} wrong vs :status: {len(wrong_status)}")
    print(f"{args.candidate} disagreements with references: {len(disagreements)}")
    print(f"reference conflicts: {len(reference_conflicts)}")
    print(f"solved only by {args.candidate}: {len(only_candidate)}")
    print(f"{args.candidate} by family:")
    for family, counts in summary["candidate_families"].items():
        detail = ", ".join(f"{key} {value}" for key, value in sorted(counts.items()))
        print(f"  {family}: {detail}")
    for item in wrong_status + disagreements:
        print(f"  PROBLEM {item}")
    return 0 if complete and not wrong_status and not disagreements else 1


if __name__ == "__main__":
    raise SystemExit(main())
