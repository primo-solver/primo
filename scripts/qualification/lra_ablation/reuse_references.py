#!/usr/bin/env python3
"""Audit historical QF_LRA CSVs and prepare an explicitly mixed-date cactus.

This reads existing timings only. Filename equality does not establish historical
input-byte identity: the old campaigns did not capture per-input SHA256 values.
Reference solves after the requested cutoff become censored rows in derived CSVs;
the original CSVs are never modified. Use plot_cactus.py on the output directories.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path

REFERENCES = {"opensmt": "119045", "yices": "118904", "z3": "118382", "cvc5": "118382"}
VERSIONS = {"opensmt": "2.9.2", "yices": "2.7.0", "z3": "5.0.0", "cvc5": "1.3.4"}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decisive(row: dict[str, str]) -> bool:
    return row["status"] == "ok" and row["answer"] in {"sat", "unsat"}


def load(paths: list[Path], hashes: dict[str, str]) -> dict[str, dict[str, str]]:
    if not paths:
        raise ValueError("missing CSVs")
    rows = {}
    for path in paths:
        hashes[str(path)] = digest(path)
        with path.open(newline="") as handle:
            for row in csv.DictReader(handle):
                name = row["file"].removeprefix("tmp/QF_LRA/")
                seconds = float(row["seconds"])
                if (name in rows or not math.isfinite(seconds) or seconds < 0
                        or row["wrong"] != "false" or row["status"] not in {"ok", "timeout"}
                        or (row["status"] == "ok" and (row["answer"] not in {"sat", "unsat"}
                            or row["exit_code"] != "0" or row["timeout"] != "false"))):
                    raise ValueError(f"duplicate or invalid outcome: {path}: {name}")
                row["file"] = name
                rows[name] = row
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--historical", type=Path, required=True,
                        help="extracted archive root containing results/qf-lra-scrambled-*")
    parser.add_argument("--native", type=Path, required=True, help="full30/plot-data directory")
    parser.add_argument("--manifest", type=Path, default=Path("benchmarks/qf-lra-full-division.txt"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or not 0 < args.timeout <= 30:
        raise ValueError("cutoff must be positive and no larger than the native 30-second budget")
    names = [line.strip() for line in args.manifest.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    names = [name.removeprefix("tmp/QF_LRA/") for name in names]
    if len(names) != 1753 or len(set(names)) != 1753:
        raise ValueError("expected the unique 1753-input full manifest")
    hashes = {str(args.manifest): digest(args.manifest)}
    series = {}
    for profile in ("default", "phase", "monotone", "both"):
        series[f"primo-{profile}"] = load([args.native / f"{profile}.csv"], hashes)
    for solver, job in REFERENCES.items():
        paths = sorted((args.historical / "results" / f"qf-lra-scrambled-{job}")
                       .glob(f"{solver}.shard-*.csv"))
        paths = [path for path in paths if not path.stem.endswith("-summary")]
        if len(paths) != 40:
            raise ValueError(f"expected 40 {solver} shards")
        series[solver] = load(paths, hashes)
    if any(set(rows) != set(names) for rows in series.values()):
        raise ValueError("filename coverage mismatch")
    pairs = []
    for first, second in itertools.combinations(series, 2):
        common = [name for name in names
                  if decisive(series[first][name]) and decisive(series[second][name])]
        if any(series[first][name]["answer"] != series[second][name]["answer"] for name in common):
            raise ValueError(f"decisive answer disagreement: {first}, {second}")
        pairs.append({"first": first, "second": second, "common_decisive": len(common),
                      "disagreements": 0})
    args.out.mkdir(parents=True, exist_ok=True)
    summary = {}
    for label, source in series.items():
        directory = args.out / label
        directory.mkdir(exist_ok=True)
        derived = []
        for name in names:
            row = dict(source[name])
            if decisive(row) and float(row["seconds"]) > args.timeout:
                row.update(status="timeout", answer="unknown", timeout="true")
            derived.append(row)
        path = directory / "shard-0.csv"
        with path.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(derived[0]))
            writer.writeheader()
            writer.writerows(derived)
        solved = [row for row in derived if decisive(row)]
        families = sorted({name.split("/", 1)[0] for name in names})
        summary[label] = {
            "inputs": len(names), "solved": len(solved), "unsolved": len(names) - len(solved),
            "errors": 0, "wrong": 0, "original_solved": sum(map(decisive, source.values())),
            "censored_solves": sum(map(decisive, source.values())) - len(solved),
            "par2_sum_seconds": round(sum(float(row["seconds"]) for row in solved)
                                      + 2 * args.timeout * (len(names) - len(solved)), 6),
            "derived_csv_sha256": digest(path),
            "families": {
                family: {
                    "inputs": sum(row["file"].split("/", 1)[0] == family for row in derived),
                    "solved": sum(row["file"].split("/", 1)[0] == family for row in solved),
                }
                for family in families
            },
        }
    audit = {
        "status": "historical-reference-reuse", "inputs": len(names), "cutoff_seconds": args.timeout,
        "native_source_git": "42005258b21263500e24faf9f0865bb213663935",
        "reference_jobs": REFERENCES, "reference_versions": VERSIONS,
        "reference_original_timeout_seconds": 300,
        "limitations": ["Historical input identity checked by filename only; original input hashes absent.",
                        "Historical references: August 2026 compute, 12 GiB; native: October 2026 compute, 7 GiB.",
                        "Derived cutoff is censoring stored timings, not a fresh 30-second reference run.",
                        "Native models separately validated; no new historical-reference model validation."],
        "summary": summary, "answer_pairs": pairs, "source_sha256": hashes,
    }
    (args.out / "audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
