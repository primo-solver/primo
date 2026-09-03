#!/usr/bin/env python3
"""Aggregate and compare durable benchmark shard CSVs.

Usage:
    python3 scripts/summarize_benchmark_shards.py \
      --timeout 30 \
      --run baseline=results/baseline \
      --run candidate=results/candidate \
      --baseline baseline --candidate candidate

Each run directory may contain any number of ``*.csv`` files in
``bench_qf_uf_full.py`` row format. Files ending in ``-summary.csv`` are
ignored. The JSON output is suitable for direct inclusion in run-log analysis.
"""

from __future__ import annotations

import argparse
import collections
import csv
import json
import sys
from pathlib import Path

DECISIVE = {"sat", "unsat"}


def load_rows(directory: Path) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for path in sorted(directory.glob("*.csv")):
        if path.stem.endswith("-summary"):
            continue
        with path.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                file = row.get("file", "")
                if not file:
                    raise ValueError(f"{path}: row has no file field")
                if file in rows:
                    raise ValueError(f"duplicate benchmark row for {file}")
                rows[file] = row
    if not rows:
        raise ValueError(f"no benchmark rows found in {directory}")
    return rows


def summarize(rows: dict[str, dict[str, str]], timeout: float) -> dict[str, object]:
    statuses = collections.Counter(row.get("status", "") for row in rows.values())
    answers = collections.Counter(
        row.get("answer", "")
        for row in rows.values()
        if row.get("status") == "ok"
    )
    wrong = sum(row.get("wrong", "").lower() == "true" for row in rows.values())
    solved_seconds = sum(
        float(row.get("seconds", 0.0))
        for row in rows.values()
        if row.get("status") == "ok"
    )
    par2 = sum(
        float(row.get("seconds", 0.0))
        if row.get("status") == "ok"
        else 2.0 * timeout
        for row in rows.values()
    )

    families: dict[str, dict[str, object]] = {}
    for family in sorted({row.get("family", "") for row in rows.values()}):
        family_rows = {file: row for file, row in rows.items() if row.get("family", "") == family}
        family_statuses = collections.Counter(
            row.get("status", "") for row in family_rows.values()
        )
        family_answers = collections.Counter(
            row.get("answer", "")
            for row in family_rows.values()
            if row.get("status") == "ok"
        )
        family_par2 = sum(
            float(row.get("seconds", 0.0))
            if row.get("status") == "ok"
            else 2.0 * timeout
            for row in family_rows.values()
        )
        families[family] = {
            "files": len(family_rows),
            "solved": family_statuses["ok"],
            "sat": family_answers["sat"],
            "unsat": family_answers["unsat"],
            "timeout": family_statuses["timeout"],
            "unsupported": family_statuses["unsupported"],
            "error": sum(
                count
                for status, count in family_statuses.items()
                if status not in {"ok", "timeout", "unsupported"}
            ),
            "par2": family_par2,
        }

    return {
        "files": len(rows),
        "solved": statuses["ok"],
        "sat": answers["sat"],
        "unsat": answers["unsat"],
        "timeout": statuses["timeout"],
        "unsupported": statuses["unsupported"],
        "error": sum(
            count
            for status, count in statuses.items()
            if status not in {"ok", "timeout", "unsupported"}
        ),
        "wrong": wrong,
        "solved_seconds": solved_seconds,
        "par2": par2,
        "nonstandard_rows": [
            {
                key: row.get(key, "")
                for key in (
                    "file",
                    "status",
                    "answer",
                    "exit_code",
                    "timed_out_with_stats",
                    "stderr_head",
                )
            }
            for row in rows.values()
            if row.get("status") not in {"ok", "timeout"}
        ],
        "families": families,
    }


def compare(
    baseline: dict[str, dict[str, str]], candidate: dict[str, dict[str, str]]
) -> dict[str, object]:
    common = sorted(set(baseline) & set(candidate))
    common_solved = [
        file
        for file in common
        if baseline[file].get("status") == "ok" and candidate[file].get("status") == "ok"
    ]
    disagreements = [
        file
        for file in common
        if baseline[file].get("status") == "ok"
        and candidate[file].get("status") == "ok"
        and baseline[file].get("answer") in DECISIVE
        and candidate[file].get("answer") in DECISIVE
        and baseline[file].get("answer") != candidate[file].get("answer")
    ]
    recoveries = [
        file
        for file in common
        if baseline[file].get("status") != "ok" and candidate[file].get("status") == "ok"
    ]
    losses = [
        file
        for file in common
        if baseline[file].get("status") == "ok" and candidate[file].get("status") != "ok"
    ]
    return {
        "common_files": len(common),
        "baseline_only": len(set(baseline) - set(candidate)),
        "candidate_only": len(set(candidate) - set(baseline)),
        "recoveries": recoveries,
        "losses": losses,
        "decisive_answer_disagreements": disagreements,
        "common_solved": len(common_solved),
        "baseline_common_solved_seconds": sum(
            float(baseline[file].get("seconds", 0.0)) for file in common_solved
        ),
        "candidate_common_solved_seconds": sum(
            float(candidate[file].get("seconds", 0.0)) for file in common_solved
        ),
    }


def parse_run(spec: str) -> tuple[str, Path]:
    name, separator, directory = spec.partition("=")
    if not separator or not name or not directory:
        raise argparse.ArgumentTypeError("--run expects NAME=DIRECTORY")
    return name, Path(directory)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, required=True)
    parser.add_argument("--run", action="append", type=parse_run, required=True)
    parser.add_argument("--baseline")
    parser.add_argument("--candidate")
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if (args.baseline is None) != (args.candidate is None):
        parser.error("--baseline and --candidate must be supplied together")

    loaded: dict[str, dict[str, dict[str, str]]] = {}
    for name, directory in args.run:
        if name in loaded:
            parser.error(f"duplicate run name: {name}")
        try:
            loaded[name] = load_rows(directory)
        except ValueError as error:
            print(error, file=sys.stderr)
            return 1

    output: dict[str, object] = {
        "timeout_seconds": args.timeout,
        "runs": {name: summarize(rows, args.timeout) for name, rows in loaded.items()},
    }
    if args.baseline is not None:
        if args.baseline not in loaded or args.candidate not in loaded:
            parser.error("comparison names must identify supplied --run entries")
        output["comparison"] = compare(loaded[args.baseline], loaded[args.candidate])

    json.dump(output, sys.stdout, indent=2, sort_keys=True)
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
