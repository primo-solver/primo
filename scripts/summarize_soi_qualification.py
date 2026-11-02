#!/usr/bin/env python3
"""Join exact SAT replay coverage and separately measured SOI diagnostics."""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

from benchmark_model_replay import digest, write_json
from run_lra_simplex_ablation import decisive, read_rows, schedule


def model_coverage(root):
    plan = json.loads((root / "plan.json").read_text())
    passed = Counter()
    expected_total = Counter()
    tools = {}
    parents = sorted((root / "timing").glob("shard-*"))
    if not parents:
        raise ValueError("missing timing parents")
    count = json.loads((parents[0] / "metadata.json").read_text())["shards"]
    if {p.name for p in parents} != {f"shard-{i}" for i in range(count)}:
        raise ValueError("missing timing parent shards")
    for parent in parents:
        pm = json.loads((parent / "metadata.json").read_text())
        if not pm["complete"] or pm["plan_sha256"] != digest(root / "plan.json"):
            raise ValueError("incomplete or changed timing parent")
        rows = read_rows(parent / "rows.jsonl")
        for binary, identity in plan["binaries"].items():
            wanted = {
                row["key"]: row["configuration"]
                for row in rows
                if row["binary"] == binary and decisive(row["run"]) == "sat"
            }
            expected_total.update(wanted.values())
            base = root / "model-selections" / binary
            selected = json.loads(
                (base / "timing" / parent.name / "metadata.json").read_text()
            )
            md = base / "models" / parent.name
            meta = json.loads((md / "metadata.json").read_text())
            if (
                not selected.get("derived_selection")
                or selected["parent_metadata_sha256"]
                != digest(parent / "metadata.json")
                or not meta["complete"]
                or meta["solver"]["sha256"] != identity["sha256"]
                or meta["passed"] != len(wanted)
            ):
                raise ValueError(f"model identity/count mismatch: {md}")
            validator = meta["dolmen"]
            if binary in tools and tools[binary] != validator:
                raise ValueError("validator changed")
            tools[binary] = validator
            joins = json.loads((md / "joins.json").read_text()) if wanted else []
            if len(joins) != len(wanted) or {j["key"] for j in joins} != set(wanted):
                raise ValueError(f"missing/duplicate/unexpected model joins: {md}")
            for join in joins:
                if (join["outcome"], join["validator"]) != ("pass", "dolmen"):
                    raise ValueError("model replay did not independently pass")
                passed[wanted[join["key"]]] += 1
    if passed != expected_total:
        raise ValueError("SAT replay coverage mismatch")
    return {"expected": dict(expected_total), "passed": dict(passed), "tools": tools}


def diagnostics(root):
    plan_path = root / "plan.json"
    plan = json.loads(plan_path.read_text())
    if plan["repeats"] != 1:
        raise ValueError("diagnostic comparison expects one repetition")
    cases = defaultdict(dict)
    coverage = defaultdict(Counter)
    hosts = Counter()
    shard_count = None
    shards = sorted((root / "profile").glob("shard-*"))
    for shard in shards:
        meta = json.loads((shard / "metadata.json").read_text())
        shard_count = meta["shards"] if shard_count is None else shard_count
        if (
            not meta["complete"]
            or meta["shards"] != shard_count
            or meta["plan_sha256"] != digest(plan_path)
            or meta["solver"]["sha256"] != plan["solver_sha256"]
        ):
            raise ValueError("incomplete diagnostic shard")
        hosts[meta["host"]] += 1
        rows = read_rows(shard / "rows.jsonl")
        wanted = [
            (n, r, c["id"])
            for n, r, c in schedule(
                plan, plan["selected_files"], meta["shard"], shard_count
            )
        ]
        if [(r["file"], r["repeat"], r["configuration"]) for r in rows] != wanted:
            raise ValueError("diagnostic schedule differs")
        for row in rows:
            cfg = row["configuration"]
            coverage[cfg][row["analysis"]["coverage"]] += 1
            if decisive(row["run"]) and row["analysis"]["coverage"] in (
                "complete",
                "no-checks",
            ):
                work = {
                    k: float(v)
                    for k, v in row["run"]["stats"].items()
                    if k.startswith(":lra.")
                }
                work["profile.check_seconds"] = (
                    row["analysis"].get("total_check_ns", 0) / 1e9
                )
                cases[cfg][row["file"]] = work
    if shard_count is None or {p.name for p in shards} != {
        f"shard-{i}" for i in range(shard_count)
    }:
        raise ValueError("missing diagnostic shards")
    common = set.intersection(*(set(cases[c["id"]]) for c in plan["configurations"]))
    # Sum published values and expose absent fields separately. A terminal
    # assertion conflict can prevent deferred publication of objective work;
    # absence is not proof of zero work. A metric unsupported by the archived
    # binary remains unknown rather than being assigned a recorded sum.
    supported = defaultdict(set)
    for config in plan["configurations"]:
        for work in cases[config["id"]].values():
            supported[config["binary"]].update(work)
    metrics = sorted({k for files in cases.values() for w in files.values() for k in w})
    result = {}
    for config in plan["configurations"]:
        cfg = config["id"]
        result[cfg] = {
            "coverage": dict(coverage[cfg]),
            "successful": len(cases[cfg]),
            "common_sums": {},
            "absent_common_inputs": {},
        }
        for metric in metrics:
            values = [cases[cfg][n].get(metric, 0) for n in sorted(common)]
            result[cfg]["common_sums"][metric] = (
                sum(values) if metric in supported[config["binary"]] else None
            )
            result[cfg]["absent_common_inputs"][metric] = (
                sum(metric not in cases[cfg][n] for n in common)
                if metric in supported[config["binary"]]
                else None
            )
    return {
        "common_successful_inputs": len(common),
        "hosts": dict(hosts),
        "configurations": result,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timing", type=Path, required=True)
    parser.add_argument("--profiles", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    write_json(
        args.out,
        {
            "analysis_sha256": digest(Path(__file__)),
            "models": model_coverage(args.timing),
            "diagnostics": diagnostics(args.profiles),
        },
    )


if __name__ == "__main__":
    main()
