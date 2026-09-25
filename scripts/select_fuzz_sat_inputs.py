#!/usr/bin/env python3
"""Select successful SAT rows for model replay, preserving original input paths."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def select(rows: list[dict[str, str]]) -> list[str]:
    paths = []
    for row in rows:
        if (row.get("outcome") != "PASS" or row.get("ours") != "sat"
                or row.get("ours_code", "0") != "0"):
            continue
        path = Path(row.get("case") or row["file"]).resolve()
        if not path.is_file():
            raise ValueError(f"missing successful SAT input: {path}")
        paths.append(str(path))
    if len(paths) != len(set(paths)):
        raise ValueError("duplicate successful SAT input")
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    with args.summary.open() as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError("empty answer summary")
    paths = select(rows)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("".join(path + "\n" for path in paths))
    print(f"selected {len(paths)} successful SAT rows from {len(rows)} answer rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
