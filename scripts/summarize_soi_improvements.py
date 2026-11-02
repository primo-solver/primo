#!/usr/bin/env python3
"""Audit SOI measurement completeness and compare paired process PAR2 by parent."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from collections import Counter, defaultdict
from pathlib import Path

from benchmark_model_replay import digest, write_json
from run_lra_simplex_ablation import check_run, decisive, read_rows, schedule
from summarize_lra_simplex_ablation import (
    clustered_difference,
    clustered_ratio,
    outcome,
)


def load_run(root):
    plan_path = root / "plan.json"
    plan = json.loads(plan_path.read_text())
    names = plan["selected_files"]
    rows = []
    shards = sorted((root / "timing").glob("shard-*"))
    if not shards:
        raise ValueError(f"no timing shards: {root}")
    count = None
    for shard in shards:
        meta = json.loads((shard / "metadata.json").read_text())
        count = meta["shards"] if count is None else count
        if (
            not meta["complete"]
            or meta["shards"] != count
            or meta["plan_sha256"] != digest(plan_path)
            or meta["solver"]["sha256"] != plan["solver_sha256"]
        ):
            raise ValueError(f"incomplete/mismatched timing metadata: {shard}")
        if "binaries" in plan and {
            key: value["sha256"] for key, value in meta["binaries"].items()
        } != {key: value["sha256"] for key, value in plan["binaries"].items()}:
            raise ValueError(f"frozen binary registry differs: {shard}")
        got = read_rows(shard / "rows.jsonl")
        expected = [
            (n, r, c["id"]) for n, r, c in schedule(plan, names, meta["shard"], count)
        ]
        if [(r["file"], r["repeat"], r["configuration"]) for r in got] != expected:
            raise ValueError(f"missing/duplicate/reordered timing rows: {shard}")
        for row in got:
            check_run(row["run"], row["expected"])
            if row["analysis"]["coverage"] != "uninstrumented":
                raise ValueError(
                    "headline process times must have phase profiling disabled"
                )
        rows.extend(got)
    if {s.name for s in shards} != {f"shard-{i}" for i in range(count)}:
        raise ValueError("missing timing shards")
    return plan, rows


def summarize(roots, historical, baselines):
    with historical.open(newline="") as stream:
        origins = {
            r["file"]: r
            for r in csv.DictReader(stream)
            if r["configuration"] == "soi-g1-f0"
        }
    groups = defaultdict(list)
    plans = {}
    answers = defaultdict(set)
    for root in roots:
        plan, rows = load_run(root)
        for config in plan["configurations"]:
            if config["id"] in plans:
                raise ValueError("configuration appears in multiple runs")
            plans[config["id"]] = plan
        for row in rows:
            groups[row["configuration"], row["file"]].append(row)
            if answer := decisive(row["run"]):
                answers[row["file"]].add(answer)
    if any(len(values) > 1 for values in answers.values()):
        raise ValueError("cross-profile/repeat answer disagreement")
    compact = defaultdict(dict)
    for (config, name), rows in groups.items():
        plan = plans[config]
        if len(rows) != plan["repeats"]:
            raise ValueError("incomplete repetitions")
        stable = all(decisive(row["run"]) for row in rows)
        penalties = [
            float(r["run"]["seconds"])
            if decisive(r["run"])
            else 2 * plan["timeout_seconds"]
            for r in rows
        ]
        keys = {
            key for r in rows for key in r["run"]["stats"] if key.startswith(":lra.")
        }
        compact[config][name] = {
            "parent": origins[name]["parent"],
            "family": origins[name]["family"],
            "stable": stable,
            "par2": statistics.mean(penalties),
            "median_par2": statistics.median(penalties),
            "seconds": statistics.median(float(r["run"]["seconds"]) for r in rows)
            if stable
            else None,
            "outcomes": dict(Counter(outcome(r["run"]) for r in rows)),
            "work": {
                key: statistics.median(
                    float(r["run"]["stats"].get(key, 0)) for r in rows
                )
                if stable
                else None
                for key in sorted(keys)
            },
        }
    report = {
        "analysis_sha256": digest(Path(__file__)),
        "runs": [str(p) for p in roots],
        "configurations": {},
        "comparisons": {},
        "per_input": compact,
    }
    machines = Counter()
    for root in roots:
        for path in (root / "timing").glob("shard-*/metadata.json"):
            meta = json.loads(path.read_text())
            machines[(meta["host"], meta["cpu_model"], len(meta["affinity"]))] += 1
    report["timing_machines"] = [
        {"host": host, "cpu_model": cpu, "affinity_logical_cpus": size, "shards": n}
        for (host, cpu, size), n in sorted(machines.items())
    ]
    for config, cases in compact.items():
        by_family = defaultdict(list)
        outcomes = Counter()
        for row in cases.values():
            by_family[row["family"]].append(row)
            outcomes.update(row["outcomes"])
        report["configurations"][config] = {
            "inputs": len(cases),
            "stable_solves": sum(r["stable"] for r in cases.values()),
            "process_par2": sum(r["par2"] for r in cases.values()),
            "outcomes": dict(outcomes),
            "families": {
                family: {
                    "inputs": len(rows),
                    "stable_solves": sum(r["stable"] for r in rows),
                    "process_par2": sum(r["par2"] for r in rows),
                }
                for family, rows in by_family.items()
            },
        }
    for baseline in baselines:
        original = compact[baseline]
        if not original:
            raise ValueError(f"missing baseline {baseline}")
        for config, cases in compact.items():
            shared = sorted(set(original) & set(cases))
            report["comparisons"][f"{config}-vs-{baseline}"] = {
                "par2_change": clustered_difference(
                    [
                        (cases[n]["parent"], cases[n]["par2"] - original[n]["par2"])
                        for n in shared
                    ],
                    20261102,
                ),
                "process_speedup": clustered_ratio(
                    [
                        (
                            cases[n]["parent"],
                            original[n]["seconds"] / cases[n]["seconds"],
                        )
                        for n in shared
                        if original[n]["stable"]
                        and cases[n]["stable"]
                        and cases[n]["seconds"] > 0
                    ],
                    20261102,
                ),
                "stable_solve_losses": [
                    n
                    for n in shared
                    if original[n]["stable"] and not cases[n]["stable"]
                ],
                "stable_solve_gains": [
                    n
                    for n in shared
                    if not original[n]["stable"] and cases[n]["stable"]
                ],
            }
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="append", type=Path, required=True)
    parser.add_argument("--historical", type=Path, required=True)
    parser.add_argument("--baseline", action="append", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    write_json(args.out, summarize(args.run, args.historical, args.baseline))


if __name__ == "__main__":
    main()
