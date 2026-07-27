#!/usr/bin/env python3
"""Summarize QF_UF disequality-propagation diagnostic counters from benchmark CSVs."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path


DISEQ_PREFIX = ":euf.diseq-prop."


def dialect(path: Path) -> csv.Dialect:
    sample = path.read_text(errors="replace")[:4096]
    try:
        return csv.Sniffer().sniff(sample, delimiters=",\t")
    except csv.Error:
        return csv.excel_tab if path.suffix == ".tsv" else csv.excel


def parse_int(value: str | None) -> int:
    if value is None or value == "":
        return 0
    try:
        return int(value)
    except ValueError:
        return 0


def read_rows(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle, dialect=dialect(path))
        if reader.fieldnames is None:
            raise SystemExit(f"{path}: missing header")
        return reader.fieldnames, list(reader)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    if args.top < 0:
        raise SystemExit("--top must be non-negative")

    fieldnames, rows = read_rows(args.csv)
    stat_keys = sorted(key for key in fieldnames if key.startswith(DISEQ_PREFIX))
    if not stat_keys:
        raise SystemExit(f"{args.csv}: no {DISEQ_PREFIX} columns found")

    totals = Counter[str]()
    active = Counter[str]()
    by_family = Counter[tuple[str, str]]()
    scored_rows: list[dict[str, object]] = []
    status_counts = Counter(row.get("status", "") for row in rows)
    wrong = sum(1 for row in rows if row.get("wrong") == "true")

    for row in rows:
        row_score = 0
        for key in stat_keys:
            value = parse_int(row.get(key))
            totals[key] += value
            if value > 0:
                active[key] += 1
            by_family[(row.get("family", ""), key)] += value
            row_score += value
        if row_score > 0:
            scored_rows.append(
                {
                    "file": row.get("file", ""),
                    "family": row.get("family", ""),
                    "status": row.get("status", ""),
                    "answer": row.get("answer", ""),
                    "seconds": row.get("seconds", ""),
                    "diagnostic_total": row_score,
                    "assigned_app_disequalities": parse_int(
                        row.get(f"{DISEQ_PREFIX}assigned-app-disequalities")
                    ),
                    "source_unary_candidates": parse_int(
                        row.get(f"{DISEQ_PREFIX}source-unary-candidates")
                    ),
                    "flat_same_function_candidates": parse_int(
                        row.get(f"{DISEQ_PREFIX}flat-same-function-candidates")
                    ),
                    "guarded_function_guard_candidates": parse_int(
                        row.get(f"{DISEQ_PREFIX}guarded-function-guard-candidates")
                    ),
                    "guarded_argument_guard_candidates": parse_int(
                        row.get(f"{DISEQ_PREFIX}guarded-argument-guard-candidates")
                    ),
                }
            )

    scored_rows.sort(
        key=lambda row: (
            int(row["diagnostic_total"]),
            int(row["assigned_app_disequalities"]),
        ),
        reverse=True,
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["section", "metric", "value"])
        writer.writerow(["summary", "input", str(args.csv)])
        writer.writerow(["summary", "total_files", len(rows)])
        for status, count in sorted(status_counts.items()):
            writer.writerow(["summary", f"status.{status}", count])
        writer.writerow(["summary", "wrong", wrong])
        for key in stat_keys:
            writer.writerow(["stat_total", key, totals[key]])
            writer.writerow(["stat_active_files", key, active[key]])
        families = sorted({family for family, _key in by_family if family})
        for family in families:
            for key in stat_keys:
                writer.writerow(["family_stat_total", f"{family}.{key}", by_family[(family, key)]])
        for index, row in enumerate(scored_rows[: args.top], start=1):
            writer.writerow(["top_file", f"{index}.file", row["file"]])
            writer.writerow(["top_file", f"{index}.family", row["family"]])
            writer.writerow(["top_file", f"{index}.status", row["status"]])
            writer.writerow(["top_file", f"{index}.answer", row["answer"]])
            writer.writerow(["top_file", f"{index}.seconds", row["seconds"]])
            writer.writerow(["top_file", f"{index}.diagnostic_total", row["diagnostic_total"]])
            writer.writerow(
                [
                    "top_file",
                    f"{index}.assigned_app_disequalities",
                    row["assigned_app_disequalities"],
                ]
            )
            writer.writerow(
                ["top_file", f"{index}.source_unary_candidates", row["source_unary_candidates"]]
            )
            writer.writerow(
                [
                    "top_file",
                    f"{index}.flat_same_function_candidates",
                    row["flat_same_function_candidates"],
                ]
            )
            writer.writerow(
                [
                    "top_file",
                    f"{index}.guarded_function_guard_candidates",
                    row["guarded_function_guard_candidates"],
                ]
            )
            writer.writerow(
                [
                    "top_file",
                    f"{index}.guarded_argument_guard_candidates",
                    row["guarded_argument_guard_candidates"],
                ]
            )

    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
