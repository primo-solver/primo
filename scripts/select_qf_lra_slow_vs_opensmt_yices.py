#!/usr/bin/env python3
"""Select QF_LRA instances where Primo trails both OpenSMT and Yices.

The input CSVs are bench_qf_uf_full.py per-file result files.  The selector is
deliberately conservative: reference solvers must both solve and agree, Primo
must not be wrong when it solves, and a solved Primo row must clear both an
absolute and a ratio gap against the slower reference.
"""

from __future__ import annotations

import argparse
import collections
import csv
import glob
from dataclasses import dataclass
from pathlib import Path

DECISIVE = {"sat", "unsat"}


@dataclass(frozen=True)
class Candidate:
    path: str
    family: str
    primo_status: str
    primo_seconds: float
    opensmt_seconds: float
    yices_seconds: float
    ratio: float
    abs_gap: float


def load_rows(pattern: str) -> dict[str, dict[str, str]]:
    paths = [
        Path(path)
        for path in sorted(glob.glob(pattern))
        if not Path(path).stem.endswith("-summary")
    ]
    if not paths:
        path = Path(pattern)
        if path.is_file():
            paths = [path]
    if not paths:
        raise SystemExit(f"no CSV files match {pattern!r}")

    rows: dict[str, dict[str, str]] = {}
    for path in paths:
        with path.open(newline="") as handle:
            for row in csv.DictReader(handle):
                if row["file"] in rows:
                    raise SystemExit(f"duplicate row for {row['file']} in {pattern!r}")
                rows[row["file"]] = row
    return rows


def solved(row: dict[str, str]) -> bool:
    return row.get("status") == "ok" and row.get("answer") in DECISIVE


def seconds(row: dict[str, str]) -> float:
    return float(row["seconds"])


def candidate_rows(
    primo: dict[str, dict[str, str]],
    opensmt: dict[str, dict[str, str]],
    yices: dict[str, dict[str, str]],
    *,
    min_ratio: float,
    min_abs_gap: float,
    timeout: float,
) -> list[Candidate]:
    candidates: list[Candidate] = []
    for path in sorted(set(primo) & set(opensmt) & set(yices)):
        p_row = primo[path]
        o_row = opensmt[path]
        y_row = yices[path]
        if not (solved(o_row) and solved(y_row)):
            continue
        if o_row["answer"] != y_row["answer"]:
            continue
        if p_row.get("wrong") == "true":
            continue
        if solved(p_row) and p_row["answer"] != o_row["answer"]:
            continue

        p_seconds = seconds(p_row) if p_row.get("status") == "ok" else timeout
        ref_seconds = max(seconds(o_row), seconds(y_row))
        ratio = p_seconds / max(ref_seconds, 0.001)
        abs_gap = p_seconds - ref_seconds
        if p_row.get("status") == "timeout" or (
            ratio >= min_ratio and abs_gap >= min_abs_gap
        ):
            candidates.append(
                Candidate(
                    path=path.removeprefix("tmp/QF_LRA/"),
                    family=p_row["family"],
                    primo_status=p_row["status"],
                    primo_seconds=p_seconds,
                    opensmt_seconds=seconds(o_row),
                    yices_seconds=seconds(y_row),
                    ratio=ratio,
                    abs_gap=abs_gap,
                )
            )
    return candidates


def rank_key(candidate: Candidate) -> tuple[int, float, float]:
    timed_out = 1 if candidate.primo_status == "timeout" else 0
    return (timed_out, candidate.abs_gap, candidate.ratio)


def choose_representative(
    candidates: list[Candidate], *, per_family: int, limit: int
) -> list[Candidate]:
    by_family: dict[str, list[Candidate]] = collections.defaultdict(list)
    for candidate in candidates:
        by_family[candidate.family].append(candidate)
    for rows in by_family.values():
        rows.sort(key=rank_key, reverse=True)

    selected: list[Candidate] = []
    families = sorted(by_family)
    for offset in range(per_family):
        for family in families:
            rows = by_family[family]
            if offset < len(rows):
                selected.append(rows[offset])
                if limit and len(selected) >= limit:
                    return selected
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--primo", required=True, help="CSV path or glob")
    parser.add_argument("--opensmt", required=True, help="CSV path or glob")
    parser.add_argument("--yices", required=True, help="CSV path or glob")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, required=True)
    parser.add_argument("--min-ratio", type=float, default=2.0)
    parser.add_argument("--min-abs-gap", type=float, default=1.0)
    parser.add_argument("--per-family", type=int, default=20)
    parser.add_argument("--limit", type=int, default=200)
    args = parser.parse_args()

    candidates = candidate_rows(
        load_rows(args.primo),
        load_rows(args.opensmt),
        load_rows(args.yices),
        min_ratio=args.min_ratio,
        min_abs_gap=args.min_abs_gap,
        timeout=args.timeout,
    )
    selected = choose_representative(
        candidates, per_family=args.per_family, limit=args.limit
    )

    counts = collections.Counter(candidate.family for candidate in selected)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="\n") as handle:
        handle.write(
            "# QF_LRA instances where Primo is slower than both OpenSMT and Yices.\n"
        )
        handle.write("# Root: tmp/QF_LRA\n")
        handle.write(
            "# Selection input: bench_qf_uf_full.py CSVs for primo, opensmt, yices.\n"
        )
        handle.write(
            "# Criteria: OpenSMT and Yices solved and agreed; Primo did not give a\n"
        )
        handle.write(
            "# wrong answer; Primo timed out or was at least "
            f"{args.min_ratio:g}x and {args.min_abs_gap:g}s slower than the slower reference.\n"
        )
        handle.write(
            f"# Representative cap: up to {args.per_family} per family, "
            f"{args.limit or 'unlimited'} total.\n"
        )
        handle.write(f"# Candidate rows: {len(candidates)}; selected: {len(selected)}.\n")
        handle.write("# Selected family counts:\n")
        for family, count in sorted(counts.items()):
            handle.write(f"#   {family}: {count}\n")
        for candidate in selected:
            handle.write(f"{candidate.path}\n")

    print(f"candidates: {len(candidates)}")
    print(f"selected: {len(selected)}")
    for family, count in sorted(counts.items()):
        print(f"{family}: {count}")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
