#!/usr/bin/env python3
"""Freeze a balanced hard stratum from untraced historical and fresh screening data."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path


def names(path):
    return [
        line.split("#", 1)[0].strip()
        for line in path.read_text().splitlines()
        if line.split("#", 1)[0].strip()
    ]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", type=Path, required=True)
    parser.add_argument("--screen", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--screen-job", required=True)
    parser.add_argument("--screen-source", required=True)
    args = parser.parse_args()
    control = names(Path("benchmarks/qf-lra-explanation-probe.txt"))
    sample = set(names(Path("benchmarks/qf-lra-stratified-sample.txt")))
    history = {}
    source = None
    for path in sorted(args.history.glob("*/answers.csv")):
        p = json.loads(path.with_name("provenance.json").read_text())
        assert (
            p["status"] == "complete"
            and p["inputs_unchanged"]
            and p["solver_args"] == ["--quiet"]
        )
        identity = {k: v for k, v in p["solver"].items() if k != "path"}
        if source is None:
            source = identity
        assert identity == source
        for row in csv.DictReader(path.open()):
            assert row["wrong"] == "false"
            if row["file"] in sample - set(control) and float(row["seconds"]) >= 5:
                history[row["file"]] = {
                    "seconds": float(row["seconds"]),
                    "sha256": p["inputs"][row["file"]],
                    "shard": path.parent.name,
                    "answers_sha256": digest(path),
                    "provenance_sha256": digest(path.with_name("provenance.json")),
                }
    eligible = []
    for path in sorted(args.screen.glob("screen-*.csv")):
        for row in csv.DictReader(path.open()):
            assert row["wrong"] == "false" and row["status"] in ("ok", "timeout")
            checks = int(row.get(":lra.online-check-calls") or 0)
            conflicts = int(row.get(":theory.conflicts") or 0)
            pivots = int(row.get(":lra.pivots") or 0)
            if (
                row["file"] in history
                and row["expected"] in ("sat", "unsat")
                and float(row["seconds"]) >= 1
                and checks > 0
                and (conflicts >= 100 or pivots >= 500)
            ):
                file = row["file"]
                checksum = digest(args.root / file)
                assert checksum == history[file]["sha256"]
                eligible.append(
                    {
                        "file": file,
                        "family": row["family"],
                        "expected": row["expected"],
                        "sha256": checksum,
                        "historical": history[file],
                        "screen": {
                            "seconds": float(row["seconds"]),
                            "status": row["status"],
                            "checks": checks,
                            "conflicts": conflicts,
                            "pivots": pivots,
                            "artifact": path.name,
                            "sha256": digest(path),
                        },
                    }
                )
    selected = []
    family_count = collections.Counter()
    for status in ("sat", "unsat"):
        groups = collections.defaultdict(list)
        for row in eligible:
            if row["expected"] == status:
                groups[row["family"]].append(row)
        for rows in groups.values():
            rows.sort(
                key=lambda r: (
                    -r["screen"]["conflicts"],
                    -r["screen"]["pivots"],
                    r["file"],
                )
            )
        count = 0
        while count < 10:
            before = count
            for family in sorted(groups):
                if groups[family] and family_count[family] < 4 and count < 10:
                    row = groups[family].pop(0)
                    selected.append(row)
                    family_count[family] += 1
                    count += 1
            assert count > before, "insufficient balanced hard cases under family cap"
    selected.sort(key=lambda r: r["file"])
    inputs = []
    for file in control:
        inputs.append(
            {
                "file": file,
                "stratum": "control",
                "sha256": digest(args.root / file),
                "timeout_seconds": 30,
                "model_timeout_seconds": 90,
            }
        )
    for row in selected:
        inputs.append(
            {
                **row,
                "stratum": "hard",
                "timeout_seconds": 120,
                "model_timeout_seconds": 300,
            }
        )
    result = {
        "schema_version": 1,
        "selection": "Historical untraced full30 default >=5s; fresh untraced6c4f9c5 5s screen reaches online LRA with >=100 conflicts or >=500 pivots; exclude34 controls; ten SAT then ten UNSAT, lexical family round-robin with maximum four per family, conflict/pivot descending within family.",
        "historical_solver": source,
        "screen_source": args.screen_source,
        "screen_job": args.screen_job,
        "causal_limit": 200000,
        "snapshot_limit": 1000,
        "control_manifest": "benchmarks/qf-lra-explanation-probe.txt",
        "control_manifest_sha256": digest(
            Path("benchmarks/qf-lra-explanation-probe.txt")
        ),
        "eligible_hard_count": len(eligible),
        "hard_families": dict(family_count),
        "inputs": inputs,
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    args.out.with_suffix(".txt").write_text("".join(r["file"] + "\n" for r in inputs))
    print(
        json.dumps(
            {
                "control": len(control),
                "hard": len(selected),
                "families": dict(family_count),
            }
        )
    )


if __name__ == "__main__":
    main()
