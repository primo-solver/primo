#!/usr/bin/env python3
"""Summarize QF_UF dynamic Ackermann clause diagnostics from benchmark CSVs."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path


PREFIX = ":euf.dynamic-ack."
SHADOW_PREFIX = ":euf.dynamic-ack.shadow."


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
    stat_keys = sorted(
        key for key in fieldnames if key.startswith(PREFIX) and not key.startswith(SHADOW_PREFIX)
    )
    if not stat_keys:
        raise SystemExit(f"{args.csv}: no non-shadow {PREFIX} columns found")

    totals = Counter[str]()
    active = Counter[str]()
    by_family = Counter[tuple[str, str]]()
    status_counts = Counter(row.get("status", "") for row in rows)
    wrong = sum(1 for row in rows if row.get("wrong") == "true")
    scored_rows: list[dict[str, object]] = []

    for row in rows:
        for key in stat_keys:
            value = parse_int(row.get(key))
            totals[key] += value
            if value > 0:
                active[key] += 1
            by_family[(row.get("family", ""), key)] += value
        scored_rows.append(
            {
                "file": row.get("file", ""),
                "family": row.get("family", ""),
                "status": row.get("status", ""),
                "answer": row.get("answer", ""),
                "seconds": row.get("seconds", ""),
                "candidates": parse_int(row.get(f"{PREFIX}candidates")),
                "candidates_inspected": parse_int(row.get(f"{PREFIX}candidates-inspected")),
                "clauses_added": parse_int(row.get(f"{PREFIX}clauses-added")),
                "unit_clauses": parse_int(row.get(f"{PREFIX}unit-clauses")),
                "conflict_clauses": parse_int(row.get(f"{PREFIX}conflict-clauses")),
                "duplicate_skips": parse_int(row.get(f"{PREFIX}duplicate-skips")),
                "nonunit_skips": parse_int(row.get(f"{PREFIX}nonunit-skips")),
                "satisfied_skips": parse_int(row.get(f"{PREFIX}satisfied-skips")),
                "width_skips": parse_int(row.get(f"{PREFIX}width-skips")),
                "cap_hits": parse_int(row.get(f"{PREFIX}cap-hits")),
                "max_width": parse_int(row.get(f"{PREFIX}max-width")),
            }
        )

    scored_rows.sort(
        key=lambda row: (
            int(row["clauses_added"]),
            int(row["unit_clauses"]) + int(row["conflict_clauses"]),
            int(row["candidates_inspected"]),
            int(row["candidates"]),
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
            for field in [
                "file",
                "family",
                "status",
                "answer",
                "seconds",
                "candidates",
                "candidates_inspected",
                "clauses_added",
                "unit_clauses",
                "conflict_clauses",
                "duplicate_skips",
                "nonunit_skips",
                "satisfied_skips",
                "width_skips",
                "cap_hits",
                "max_width",
            ]:
                writer.writerow(["top_file", f"{index}.{field}", row[field]])

    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
