#!/usr/bin/env python3
"""Select two size-bounded real-derived conjunction seeds per frozen family."""

from __future__ import annotations

import argparse
import csv
import shutil
from collections import defaultdict
from pathlib import Path

from benchmark_model_replay import digest, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--historical", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    groups = defaultdict(list)
    with args.historical.open(newline="") as stream:
        for row in csv.DictReader(stream):
            if row["configuration"] != "soi-g1-f0" or row["expected"] == "unknown":
                continue
            path = args.corpus / row["file"]
            if path.stat().st_size > 200000:
                continue
            quick = (
                row["successful_repeats"] == "3"
                and float(row["process_seconds"] or 999) < 1
            )
            groups[row["family"]].append(
                (not quick, path.stat().st_size, row["file"], row)
            )
    selected = []
    for family, rows in sorted(groups.items()):
        for slow, size, name, row in sorted(rows)[:2]:
            source = args.corpus / name
            checksum = digest(source)
            if checksum != source.stem:
                raise ValueError("seed source hash mismatch")
            destination = args.out / f"{family}__{source.name}"
            shutil.copyfile(source, destination)
            selected.append(
                {
                    "file": destination.name,
                    "source": name,
                    "sha256": checksum,
                    "family": family,
                    "parent": row["parent"],
                    "bytes": size,
                    "slow_fallback": slow,
                }
            )
    write_json(
        args.out / "selection.json",
        {
            "historical_sha256": digest(args.historical),
            "manifest_sha256": digest(args.corpus / "manifest.txt"),
            "selection": selected,
        },
    )


if __name__ == "__main__":
    main()
