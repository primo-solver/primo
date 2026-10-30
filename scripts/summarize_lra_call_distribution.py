#!/usr/bin/env python3
"""Merge call-profile shards, retaining zero calls and censored observations."""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter, defaultdict
from pathlib import Path


def aggregate(records):
    pivots, bins, joint = Counter(), defaultdict(Counter), Counter()
    for row in records:
        analysis = row["analysis"]
        pivots.update(
            {int(k): v for k, v in analysis.get("pivot_histogram", {}).items()}
        )
        joint.update(analysis.get("joint_time_bit_length", {}))
        for group, values in analysis.get("pivot_bins", {}).items():
            for metric in ("calls", "sum_ns", "conflicts"):
                bins[group][metric] += values[metric]
    n = sum(pivots.values())
    quantiles = {}
    for name, fraction in [("p50", 0.5), ("p90", 0.9), ("p99", 0.99)]:
        target, seen = math.ceil(n * fraction), 0
        for value, count in sorted(pivots.items()):
            seen += count
            if seen >= target:
                quantiles[name] = value
                break
    return {
        "inputs": len(records),
        "native_outcomes": dict(Counter(outcome(row["native"]) for row in records)),
        "reference_outcomes": dict(
            Counter(outcome(row["reference"]) for row in records)
        ),
        "coverage": dict(Counter(row["analysis"]["coverage"] for row in records)),
        "inputs_with_calls": sum(row["analysis"]["calls"] > 0 for row in records),
        "calls": n,
        "zero_pivot_calls": pivots[0],
        "total_pivots": sum(k * v for k, v in pivots.items()),
        "total_check_seconds": sum(v["sum_ns"] for v in bins.values()) / 1e9,
        "max_pivots": max(pivots, default=0),
        "pivot_quantiles": quantiles,
        "pivot_histogram": dict(sorted(pivots.items())),
        "pivot_bins": dict(
            sorted(bins.items(), key=lambda item: int(item[0].split("-")[0]))
        ),
        "joint_time_bit_length": dict(joint),
    }


def outcome(run):
    return run["answer"] if run["status"] == "ok" else run["status"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--plot", type=Path)
    args = parser.parse_args()
    shards = [
        json.loads(path.read_text())
        for path in sorted(args.directory.glob("shard-*/summary.json"))
    ]
    assert shards and len(shards) == shards[0]["shards"]
    for shard in shards:
        assert shard["complete"]
        assert shard.get("solver_args", []) == shards[0].get("solver_args", [])
        for key in (
            "solver_sha256",
            "reference_sha256",
            "version",
            "manifest_sha256",
            "timeout_seconds",
            "workflow_sha256",
        ):
            assert shard[key] == shards[0][key], key
    records = [row for shard in shards for row in shard["records"]]
    assert len({row["file"] for row in records}) == len(records)
    assert not any(row["wrong"] for row in records)
    portable = []
    for row in records:
        portable.append(
            {
                key: row[key]
                for key in (
                    "index",
                    "file",
                    "family",
                    "input_sha256",
                    "expected",
                    "analysis",
                    "trace_sha256",
                )
            }
        )
        portable[-1].update(
            {
                name: {
                    key: row[name][key]
                    for key in ("status", "answer", "seconds", "exit_code", "stats")
                }
                for name in ("native", "reference")
            }
        )
    # Raw shards retain all345 counters; the portable report keeps study counters.
    relevant = {
        ":input.assertions",
        ":lra.pivots",
        ":lra.bland-fallbacks",
        ":lra.online-check-calls",
        ":lra.pivot-row-updates",
        ":lra.pivot-term-ops",
        ":sat.vars",
        ":sat.clauses",
        ":sat.conflicts",
        ":sat.decisions",
        ":sat.propagations",
    }
    for row in portable:
        for name in ("native", "reference"):
            row[name]["stats"] = {
                k: v for k, v in row[name]["stats"].items() if k in relevant
            }
    result = {
        key: shards[0][key]
        for key in (
            "solver_sha256",
            "reference_sha256",
            "version",
            "manifest_sha256",
            "timeout_seconds",
            "workflow_sha256",
        )
    }
    result.update(
        {
            "schema": 1,
            "solver_args": shards[0].get("solver_args", []),
            "job_ids": [shard["job_id"] for shard in shards],
            "all_observed": aggregate(records),
            "completed_sources": aggregate(
                [
                    row
                    for row in records
                    if row["analysis"]["coverage"] in ("complete", "no-checks")
                ]
            ),
            "censored_sources": aggregate(
                [
                    row
                    for row in records
                    if row["analysis"]["coverage"] not in ("complete", "no-checks")
                ]
            ),
            "families": {
                family: aggregate([row for row in records if row["family"] == family])
                for family in sorted({row["family"] for row in records})
            },
            "records": sorted(portable, key=lambda row: row["index"]),
        }
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    if args.plot:
        plot(result, args.plot)
    print(json.dumps(result["all_observed"], indent=2))


def plot(result, path):
    # Optional reporting dependency only; SLURM collection remains stdlib-only.
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    bins = result["all_observed"]["pivot_bins"]
    names = list(bins)
    fig, axes = plt.subplots(1, 3, figsize=(16, 4.5), layout="constrained")
    groups = [
        ("Completed source inputs", "#3178a8", "completed_sources"),
        ("Timeout prefixes", "#c17c34", "censored_sources"),
    ]
    for index, (label, color, key) in enumerate(groups):
        values = result[key]["pivot_bins"]
        counts = [values.get(name, {}).get("calls", 0) for name in names]
        positions = [i + (index - 0.5) * 0.42 for i in range(len(names))]
        axes[0].bar(positions, counts, width=0.4, label=label, color=color)
    axes[0].set_xticks(range(len(names)), names)
    axes[0].set_yscale("log")
    axes[0].set_ylim(bottom=0.5)
    axes[0].set_ylabel("Observed completed calls (log scale)")
    axes[0].legend(fontsize=8)
    axes[1].bar(names, [bins[name]["sum_ns"] / 1e9 for name in names], color="#3178a8")
    axes[1].set_ylabel("Total time inside completed checks (seconds)")
    joint = result["all_observed"]["joint_time_bit_length"]
    max_bit = max(int(key.split("|")[1]) for key in joint)
    grid = [[0.0] * len(names) for _ in range(max_bit + 1)]
    for key, count in joint.items():
        name, bit = key.split("|")
        grid[int(bit)][names.index(name)] = math.log10(count + 1)
    heat = axes[2].imshow(grid, origin="lower", aspect="auto", cmap="Blues")
    axes[2].set_xticks(range(len(names)), names)
    ticks = list(range(0, max_bit + 1, 4))
    axes[2].set_yticks(
        ticks, [f"{(2 ** (bit - 1) if bit else 0) / 1e6:.3g}" for bit in ticks]
    )
    axes[2].set_ylabel("Check duration bucket lower edge (ms)")
    fig.colorbar(heat, ax=axes[2], label="log10(calls + 1)", shrink=0.8)
    for ax in axes:
        ax.set_xlabel("Pivots in one warm check")
        ax.tick_params(axis="x", rotation=65, labelsize=8)
        ax.spines[["top", "right"]].set_visible(False)
    fig.suptitle(
        f"{result['all_observed']['inputs']} QF_LRA inputs; {result['timeout_seconds']:g}s per solver per input"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=160)
    plt.close(fig)


if __name__ == "__main__":
    main()
