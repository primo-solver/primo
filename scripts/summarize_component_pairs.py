#!/usr/bin/env python3
"""Audit paired migration runs without mixing model replay into speed metrics."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


def solved(row: dict[str, str]) -> bool:
    return (
        row.get("status") == "ok"
        and row.get("answer") in {"sat", "unsat"}
        and str(row.get("exit_code")) == "0"
        and row.get("timeout") == "false"
    )


def load_arm(root: Path) -> tuple[dict[str, dict[str, str]], dict[str, dict]]:
    rows: dict[str, dict[str, str]] = {}
    provenance: dict[str, dict] = {}
    for shard in sorted(root.glob("shard-*")):
        metadata = json.loads((shard / "provenance.json").read_text())
        if metadata["status"] != "complete" or not metadata["inputs_unchanged"]:
            raise ValueError(f"incomplete timing shard: {shard}")
        with (shard / "answers.csv").open() as stream:
            selected = list(csv.DictReader(stream))
        if len(selected) != len(metadata["inputs"]) or {
            row["file"] for row in selected
        } != set(metadata["inputs"]):
            raise ValueError(f"timing rows do not cover input provenance: {shard}")
        for row in selected:
            if row["file"] in rows:
                raise ValueError(f"duplicate input: {row['file']}")
            rows[row["file"]] = row
        provenance[shard.name] = metadata
    return rows, provenance


def metrics(rows: list[dict[str, str]], timeout: float) -> dict:
    statuses = Counter(row["status"] for row in rows)
    return {
        "rows": len(rows),
        "solved": sum(solved(row) for row in rows),
        "sat": sum(solved(row) and row["answer"] == "sat" for row in rows),
        "unsat": sum(solved(row) and row["answer"] == "unsat" for row in rows),
        "timeout": statuses["timeout"],
        "error": statuses["error"],
        "unsupported": statuses["unsupported"],
        "unknown": sum(
            row["status"] == "ok" and row["answer"] == "unknown" for row in rows
        ),
        "wrong_answer": sum(row.get("wrong") == "true" for row in rows),
        "par2": sum(
            float(row["seconds"]) if solved(row) else 2 * timeout for row in rows
        ),
    }


def compare(
    baseline: dict[str, dict[str, str]],
    candidate: dict[str, dict[str, str]],
    timeout: float,
) -> dict:
    if set(baseline) != set(candidate):
        raise ValueError("paired arms cover different inputs")
    common = [
        name for name in baseline if solved(baseline[name]) and solved(candidate[name])
    ]
    old = metrics(list(baseline.values()), timeout)
    new = metrics(list(candidate.values()), timeout)
    old_seconds = sum(float(baseline[name]["seconds"]) for name in common)
    new_seconds = sum(float(candidate[name]["seconds"]) for name in common)
    return {
        "baseline": old,
        "candidate": new,
        "disagreements": [
            name
            for name in common
            if baseline[name]["answer"] != candidate[name]["answer"]
        ],
        "lost": [
            name
            for name in baseline
            if solved(baseline[name]) and not solved(candidate[name])
        ],
        "gained": [
            name
            for name in baseline
            if not solved(baseline[name]) and solved(candidate[name])
        ],
        "common_solved": len(common),
        "baseline_common_seconds": old_seconds,
        "candidate_common_seconds": new_seconds,
        "par2_ratio": new["par2"] / old["par2"] if old["par2"] else None,
        "common_runtime_ratio": new_seconds / old_seconds if old_seconds else None,
    }


def summarize(root: Path, expected_count: int, limit: float) -> dict:
    rounds = []
    totals = {"baseline": {}, "candidate": {}}
    identities = {"baseline": set(), "candidate": set()}
    expected_files = None
    timeout = None
    for round_number in (0, 1):
        old, old_metadata = load_arm(root / f"round-{round_number}" / "baseline")
        new, new_metadata = load_arm(root / f"round-{round_number}" / "candidate")
        if (
            len(old) != expected_count
            or len(new) != expected_count
            or set(old_metadata) != set(new_metadata)
        ):
            raise ValueError("missing paired inputs or shards")
        if expected_files is not None and set(old) != expected_files:
            raise ValueError("paired rounds cover different inputs")
        expected_files = set(old)
        for shard, old_meta in old_metadata.items():
            new_meta = new_metadata[shard]
            for field in ("inputs", "solver_args", "timeout", "shards", "shard_index"):
                if old_meta[field] != new_meta[field]:
                    raise ValueError(f"paired provenance mismatch: {field}")
            if timeout is not None and timeout != old_meta["timeout"]:
                raise ValueError("mixed timing budgets")
            timeout = old_meta["timeout"]
            node = root / f"node-{old_meta['shard_index']}.txt"
            if not node.is_file() or not node.read_text().strip():
                raise ValueError("missing paired node record")
            for arm, metadata in (("baseline", old_meta), ("candidate", new_meta)):
                identities[arm].add(metadata["solver"]["sha256"])
        for arm, rows in (("baseline", old), ("candidate", new)):
            totals[arm].update(
                {f"round-{round_number}:{name}": row for name, row in rows.items()}
            )
        rounds.append(compare(old, new, timeout))
    if any(len(values) != 1 for values in identities.values()):
        raise ValueError("solver identity changed between timing shards")
    aggregate = compare(totals["baseline"], totals["candidate"], timeout)
    correctness_ok = not aggregate["disagreements"] and all(
        aggregate[arm]["wrong_answer"] == 0 and aggregate[arm]["error"] == 0
        for arm in ("baseline", "candidate")
    )
    ratios_ok = all(
        aggregate[key] is not None and aggregate[key] <= limit
        for key in ("par2_ratio", "common_runtime_ratio")
    )
    families = {}
    for family in sorted({row["family"] for row in totals["baseline"].values()}):
        selected = {
            arm: {name: row for name, row in rows.items() if row["family"] == family}
            for arm, rows in totals.items()
        }
        families[family] = compare(selected["baseline"], selected["candidate"], timeout)
    return {
        "timing_status": "pass"
        if correctness_ok and ratios_ok and not aggregate["lost"]
        else "needs-triage",
        "note": "Timing only; independent SAT-model coverage and recorded-reference joins are separate acceptance gates. Any lost solve requires three paired reruns; loss in at least two blocks acceptance.",
        "identities": {arm: sorted(values) for arm, values in identities.items()},
        "expected_unique_inputs": expected_count,
        "timeout": timeout,
        "ratio_limit": limit,
        "rounds": rounds,
        "aggregate": aggregate,
        "families": families,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--expected-count", type=int, required=True)
    parser.add_argument("--ratio-limit", type=float, default=1.01)
    args = parser.parse_args()
    try:
        report = summarize(args.root, args.expected_count, args.ratio_limit)
    except (OSError, ValueError, KeyError) as error:
        report = {"timing_status": "incomplete", "diagnostic": str(error)}
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["timing_status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
