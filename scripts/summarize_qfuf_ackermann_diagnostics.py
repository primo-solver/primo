#!/usr/bin/env python3
"""Summarize QF_UF Ackermannization cost diagnostics from benchmark CSVs."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path

ACK_PREFIX = ":euf.ackermann."


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
    stat_keys = sorted(key for key in fieldnames if key.startswith(ACK_PREFIX))
    if not stat_keys:
        raise SystemExit(f"{args.csv}: no {ACK_PREFIX} columns found")

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
                "applications": parse_int(row.get(f"{ACK_PREFIX}applications")),
                "function_symbols": parse_int(row.get(f"{ACK_PREFIX}function-symbols")),
                "eligible_symbols": parse_int(row.get(f"{ACK_PREFIX}eligible-symbols")),
                "estimated_constraints": parse_int(
                    row.get(f"{ACK_PREFIX}estimated-constraints")
                ),
                "estimated_equalities": parse_int(
                    row.get(f"{ACK_PREFIX}estimated-equalities")
                ),
                "max_symbol_applications": parse_int(
                    row.get(f"{ACK_PREFIX}max-symbol-applications")
                ),
                "symbols_cost_le_100": parse_int(
                    row.get(f"{ACK_PREFIX}symbols-cost-le-100")
                ),
                "symbols_cost_le_1000": parse_int(
                    row.get(f"{ACK_PREFIX}symbols-cost-le-1000")
                ),
                "symbols_cost_le_10000": parse_int(
                    row.get(f"{ACK_PREFIX}symbols-cost-le-10000")
                ),
            }
        )

    scored_rows.sort(
        key=lambda row: (
            int(row["estimated_equalities"]),
            int(row["estimated_constraints"]),
            int(row["applications"]),
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
                writer.writerow(
                    ["family_stat_total", f"{family}.{key}", by_family[(family, key)]]
                )
        for index, row in enumerate(scored_rows[: args.top], start=1):
            for field in [
                "file",
                "family",
                "status",
                "answer",
                "seconds",
                "applications",
                "function_symbols",
                "eligible_symbols",
                "estimated_constraints",
                "estimated_equalities",
                "max_symbol_applications",
                "symbols_cost_le_100",
                "symbols_cost_le_1000",
                "symbols_cost_le_10000",
            ]:
                writer.writerow(["top_file", f"{index}.{field}", row[field]])

    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
