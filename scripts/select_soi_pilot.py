#!/usr/bin/env python3
"""Freeze a deterministic 100-input SOI pilot before running candidate profiles."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rank(seed, value):
    return hashlib.sha256(
        json.dumps([seed, value], sort_keys=True).encode()
    ).hexdigest()


def select(rows, seed, count=100):
    groups = defaultdict(list)
    for row in rows:
        fallback = (
            "censored"
            if row["successful_repeats"] != "3"
            else "yes"
            if float(row["fallbacks"] or 0) > 0
            else "no"
        )
        key = (row["family"], row["expected"], row["stratum"], fallback)
        groups[key].append(row)
    ordered = sorted(groups, key=lambda key: rank(seed, key))
    for key in ordered:
        groups[key].sort(key=lambda row: rank(seed, row["file"]))
    picked = []
    position = 0
    while len(picked) < count:
        made_progress = False
        for key in ordered:
            if position < len(groups[key]):
                row = groups[key][position]
                picked.append(
                    {"file": row["file"], "stratum": list(key), "parent": row["parent"]}
                )
                made_progress = True
                if len(picked) == count:
                    break
        if not made_progress:
            raise ValueError("not enough distinct pilot inputs")
        position += 1
    return picked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--historical", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=20261102)
    args = parser.parse_args()
    with args.historical.open(newline="") as stream:
        rows = [
            row for row in csv.DictReader(stream) if row["configuration"] == "soi-g1-f0"
        ]
    names = args.manifest.read_text().splitlines()
    if (
        len(names) != 1000
        or len(set(names)) != 1000
        or len(rows) != 1000
        or {r["file"] for r in rows} != set(names)
    ):
        raise ValueError(
            "historical rows must cover exactly the frozen 1000-input manifest"
        )
    selected = select(rows, args.seed)
    plan = {
        "schema": 1,
        "seed": args.seed,
        "selection": "Round-robin over family,outcome,historical pivot-range,previous SOI fallback strata; hash-order strata and inputs within each stratum. Prior incomplete triples are censored, not zero fallback.",
        "historical_sha256": digest(args.historical),
        "manifest_sha256": digest(args.manifest),
        "candidate_budgets": [4, 16, 64],
        "stall_limits": [8, 16, 32],
        "pilot_selection_rule": "Compare all nine pairs on these 100 inputs with Gaussian elimination on and production fixed elimination off. Freeze lowest process PAR2 among configurations losing no stable solves against paper selection; break ties by budget then stall limit. If none qualifies, retain 16/16 as experimental only.",
        "full_evaluation": {
            "inputs": 1000,
            "repeats": 3,
            "timeout_seconds": 15,
            "gaussian_elimination": True,
            "fixed_elimination": [False, True],
            "acceptance": "No correctness failures; no lost stable solves on production fixed-elimination-off setting; paired parent-bootstrap interval for process PAR2 change excludes zero in the improving direction. Otherwise experimental only. Include successive profiles and final leave-one-out arms, separate native+Dolmen SAT validation.",
        },
        "selected": selected,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(plan, indent=2) + "\n")


if __name__ == "__main__":
    main()
