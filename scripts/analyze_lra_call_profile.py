#!/usr/bin/env python3
"""Validate and summarize completed warm numerical-feasibility calls."""

from __future__ import annotations

import argparse
import json
import math
from array import array
from collections import Counter, defaultdict
from pathlib import Path


def pivot_bin(value):
    if value < 2:
        return str(value)
    low = 1 << (value.bit_length() - 1)
    return f"{low}-{2 * low - 1}"


def quantile(values, fraction):
    return values[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


def analyze(path, command_status="ok"):
    if not path.exists():
        return {"coverage": "missing", "calls": 0}
    checkers = {}
    pivot_counts = Counter()
    timings = defaultdict(lambda: array("Q"))
    conflicts = Counter()
    joint = Counter()
    exports = []
    largest = []
    calls = total_pivots = total_ns = 0
    begin = end = None
    partial = False
    with path.open() as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                if line.endswith("\n"):
                    raise ValueError("malformed complete trace line") from None
                partial = True
                break
            if end is not None:
                raise ValueError("records after end")
            event = row["event"]
            if event == "begin":
                if begin is not None or checkers or calls:
                    raise ValueError("duplicate or late begin")
                begin = row
            elif begin is None:
                raise ValueError("missing begin")
            elif event == "checker":
                if row["checker"] != len(checkers) + 1:
                    raise ValueError("invalid checker sequence")
                checkers[row["checker"]] = {**row, "calls": 0}
            elif event == "check":
                checker = checkers[row["checker"]]
                if row["call"] != checker["calls"] + 1:
                    raise ValueError("invalid call sequence")
                checker["calls"] += 1
                pivots, ns = row["pivots"], row["nanoseconds"]
                if pivots < 0 or ns < 0:
                    raise ValueError("negative metric")
                calls += 1
                total_pivots += pivots
                total_ns += ns
                pivot_counts[pivots] += 1
                group = pivot_bin(pivots)
                timings[group].append(ns)
                conflicts[group] += row["conflict"]
                joint[f"{group}|{ns.bit_length()}"] += 1
                if len(largest) < 10 or pivots > largest[-1]["pivots"]:
                    largest.append(row)
                    largest.sort(
                        key=lambda r: (r["pivots"], r["nanoseconds"]), reverse=True
                    )
                    del largest[10:]
                if row["export"].endswith(".smt2"):
                    exports.append(row)
            elif event == "end":
                end = row
                if (end["calls"], end["checkers"], end["exported"]) != (
                    calls,
                    len(checkers),
                    len(exports),
                ):
                    raise ValueError("terminal counts disagree with recorded events")
            else:
                raise ValueError("unknown trace event: " + event)
    coverage = (
        "complete"
        if end is not None and not partial and command_status == "ok"
        else "prefix"
    )
    if not checkers and coverage == "complete":
        coverage = "no-checks"
    result = {
        "coverage": coverage,
        "has_end": end is not None,
        "partial_line": partial,
        "calls": calls,
        "checkers": list(checkers.values()),
        "total_pivots": total_pivots,
        "total_check_ns": total_ns,
        "zero_pivot_calls": pivot_counts[0],
        "conflict_calls": sum(conflicts.values()),
        "max_pivots": max(pivot_counts, default=0),
        "pivot_histogram": dict(sorted(pivot_counts.items())),
        "pivot_bins": {},
        "joint_time_bit_length": dict(joint),
        "largest_calls": largest,
        "exports": exports,
    }
    for group, values in timings.items():
        values = sorted(values)
        result["pivot_bins"][group] = {
            "calls": len(values),
            "conflicts": conflicts[group],
            "sum_ns": sum(values),
            "min_ns": values[0],
            "p50_ns": quantile(values, 0.5),
            "p90_ns": quantile(values, 0.9),
            "p99_ns": quantile(values, 0.99),
            "max_ns": values[-1],
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--command-status",
        required=True,
        choices=["ok", "timeout", "error", "unsupported"],
    )
    args = parser.parse_args()
    args.out.write_text(
        json.dumps(analyze(args.trace, args.command_status), indent=2) + "\n"
    )


if __name__ == "__main__":
    main()
