#!/usr/bin/env python3
"""Join sharded timing/reference/model results without hiding missing coverage.

Layout: ROOT/LOGIC/{primo,z3,cvc5}/shard-N, as produced by
benchmark_model_replay.py. Output is JSON; incomplete JSONL journals may be read
for progress, but never make a shard complete. This does not modify artifacts.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


def decisive(row: dict) -> bool:
    return (
        row.get("status") == "ok"
        and row.get("answer") in {"sat", "unsat"}
        and str(row.get("exit_code")) == "0"
        and str(row.get("timeout")).lower() == "false"
    )


def summarize(rows: list[dict], timeout: float) -> dict:
    solved = [row for row in rows if decisive(row)]
    return {
        "rows": len(rows),
        "solved": len(solved),
        "sat": sum(row["answer"] == "sat" for row in solved),
        "unsat": sum(row["answer"] == "unsat" for row in solved),
        "statuses": dict(Counter(row["status"] for row in rows)),
        "unknown": sum(row["answer"] == "unknown" for row in rows),
        "wrong": sum(str(row.get("wrong")).lower() == "true" for row in rows),
        "par2": sum(
            float(row["seconds"]) if decisive(row) else 2 * timeout for row in rows
        ),
    }


def load_run(root: Path, expected: int, timeout: float) -> tuple[dict, dict]:
    rows = {}
    issues = []
    model_issues = []
    complete = 0
    shard_indices = set()
    shard_counts = set()
    model_counts = Counter()
    model_rows = {}
    manifests = {}
    for shard in sorted(root.glob("shard-*")):
        provenance_path = shard / "provenance.json"
        if not provenance_path.exists():
            issues.append(f"{shard.name}: missing provenance")
            continue
        provenance = json.loads(provenance_path.read_text())
        index = provenance["shard_index"]
        if index in shard_indices:
            issues.append(f"{shard.name}: duplicate shard index")
        shard_indices.add(index)
        shard_counts.add(provenance["shards"])
        inputs = provenance["inputs"]
        if set(manifests) & set(inputs):
            issues.append(f"{shard.name}: overlapping manifests")
        manifests.update(inputs)
        source = shard / "answers.csv"
        if source.exists():
            with source.open() as stream:
                part = list(csv.DictReader(stream))
        else:
            journal = shard / "answers.csv.jsonl"
            part = []
            if journal.exists():
                lines = journal.read_text().splitlines(keepends=True)
                for line in lines:
                    if line.endswith("\n"):
                        part.append(json.loads(line))
        names = [row["file"] for row in part]
        if len(names) != len(set(names)) or set(names) & set(rows):
            issues.append(f"{shard.name}: duplicate rows")
        rows.update((row["file"], row) for row in part)
        if (
            provenance["status"] == "complete"
            and source.exists()
            and len(names) == len(set(names))
            and set(names) == set(inputs)
        ):
            complete += 1
        else:
            issues.append(f"{shard.name}: timing incomplete")
        coverage = shard / "model-validation" / "coverage.json"
        if coverage.exists():
            model_counts[json.loads(coverage.read_text())["status"]] += 1
        models = shard / "model-validation" / "models" / "summary.csv"
        if models.exists():
            with models.open() as stream:
                for row in csv.DictReader(stream):
                    name = str(Path(row["file"]).relative_to(provenance["root"]))
                    if name in model_rows:
                        model_issues.append(f"{shard.name}: duplicate model row: {name}")
                    model_rows[name] = row
    if len(shard_counts) != 1 or shard_indices != set(range(next(iter(shard_counts), 0))):
        issues.append("missing or inconsistent shard set")
    if len(manifests) != expected or len(rows) != expected:
        issues.append("missing or unexpected input coverage")
    selected = {name for name, row in rows.items() if decisive(row) and row["answer"] == "sat"}
    if set(model_rows) - selected:
        model_issues.append("unexpected model rows")
    model_summary = {
        "expected_sat": len(selected),
        "completed": len(model_rows),
        "missing": len(selected - set(model_rows)),
        "outcomes": dict(Counter(row["outcome"] for row in model_rows.values())),
        "shard_states": dict(model_counts),
        "coverage_issues": model_issues,
        "fully_validated": (
            not issues
            and not model_issues
            and complete > 0
            and model_counts == {"complete": complete}
            and set(model_rows) == selected
            and all(row["outcome"] == "pass" and row["validator"] == "dolmen"
                    and row["answer"] == "sat" for row in model_rows.values())
        ),
    }
    summary = {
        **summarize(list(rows.values()), timeout),
        "expected": expected,
        "complete_shards": complete,
        "coverage_issues": issues,
        "timing_complete": not issues,
        "families": {},
        "models": model_summary,
        "exception_rows": [row for row in rows.values()
                           if row["status"] not in {"ok", "timeout"}
                           or str(row.get("wrong")).lower() == "true"],
    }
    for family in sorted({row["family"] for row in rows.values()}):
        members = {name: row for name, row in rows.items() if row["family"] == family}
        summary["families"][family] = {
            **summarize(list(members.values()), timeout),
            "models": dict(Counter(model_rows[name]["outcome"]
                                   for name in members if name in model_rows)),
        }
    return summary, rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--expected", action="append", required=True, metavar="LOGIC=COUNT")
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    result = {}
    for specification in args.expected:
        logic, count = specification.split("=", 1)
        runs = {}
        raw = {}
        for solver in ("primo", "z3", "cvc5"):
            runs[solver], raw[solver] = load_run(args.root / logic / solver, int(count), args.timeout)
        comparisons = {}
        for left, right in (("primo", "z3"), ("primo", "cvc5"), ("z3", "cvc5")):
            common = set(raw[left]) & set(raw[right])
            comparisons[f"{left}/{right}"] = {
                "common_rows": len(common),
                "jointly_decisive": sum(decisive(raw[left][name]) and decisive(raw[right][name])
                                        for name in common),
                "disagreements": sorted(name for name in common
                                        if decisive(raw[left][name]) and decisive(raw[right][name])
                                        and raw[left][name]["answer"] != raw[right][name]["answer"]),
            }
        result[logic] = {"runs": runs, "comparisons": comparisons}
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
