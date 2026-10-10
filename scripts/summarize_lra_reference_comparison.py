#!/usr/bin/env python3
"""Audit the five-solver QF_LRA default comparison and prepare cactus CSVs.

Reuse the shared status-aware campaign summaries, then independently require
matching input hashes, solver identities, resource limits and timing CPUs.
Only the native Primo arm requires the separate Dolmen model stage.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
from pathlib import Path

from summarize_model_campaign import decisive, load_run

PROFILES = ("primo", "opensmt", "yices", "z3", "cvc5")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_models(shard: Path, metadata: dict, rows: list[dict]) -> None:
    """Require exact SAT coverage and unchanged parent/validator artifacts."""
    directory = shard / "model-validation"
    coverage = json.loads((directory / "coverage.json").read_text())
    if (
        coverage["status"] != "complete"
        or not coverage["inputs_and_binary_unchanged"]
        or coverage["validator_exit"] != 0
        or coverage["missing"]
        or coverage["malformed_rows"]
    ):
        raise ValueError(f"incomplete model coverage: {shard}")
    for filename, key in (
        ("answers.csv", "parent_answers_sha256"),
        ("provenance.json", "parent_provenance_sha256"),
    ):
        if digest(shard / filename) != coverage["provenance"][key]:
            raise ValueError(f"changed model parent: {shard}/{filename}")
    summary = directory / "models" / "summary.csv"
    if digest(summary) != coverage["artifacts"]["models/summary.csv"]:
        raise ValueError(f"changed model summary: {summary}")
    selected = {row["file"] for row in rows if decisive(row) and row["answer"] == "sat"}
    with summary.open() as stream:
        models = list(csv.DictReader(stream))
    names = [str(Path(row["file"]).relative_to(metadata["root"])) for row in models]
    if (
        len(models) != len(selected)
        or set(names) != selected
        or coverage["expected_sat"] != len(selected)
        or coverage["completed"] != len(selected)
        or coverage["dolmen_pass"] != len(selected)
        or not all(
            row["answer"] == "sat"
            and row["outcome"] == "pass"
            and row["validator"] == "dolmen"
            and row["solver_exit"] == "0"
            and row["dolmen_exit"] == "0"
            for row in models
        )
    ):
        raise ValueError(f"model rows do not certify every successful SAT: {shard}")


def collect(
    root: Path, expected: int, shards: int, timeout: float, plot_data: Path
) -> dict:
    profiles, raw, manifests, identities = {}, {}, {}, {}
    issues = []
    for name in PROFILES:
        profiles[name], raw[name] = load_run(root / name, expected, timeout)
        if any(
            row["status"] == "ok"
            and row["answer"] in {"sat", "unsat"}
            and not decisive(row)
            for row in raw[name].values()
        ):
            issues.append(f"decisive answer with unsuccessful process status: {name}")
        manifests[name] = {}
        identities[name] = []
        for index in range(shards):
            shard = root / name / f"shard-{index}"
            provenance = shard / "provenance.json"
            if not provenance.exists():
                continue
            metadata = json.loads(provenance.read_text())
            if metadata["status"] != "complete":
                continue
            try:
                if (
                    metadata["shards"] != shards
                    or metadata["shard_index"] != index
                    or metadata["timeout"] != timeout
                    or not metadata["inputs_unchanged"]
                    or set(manifests[name]) & set(metadata["inputs"])
                ):
                    raise ValueError(f"inconsistent timing provenance: {shard}")
                execution = json.loads((shard / "execution-benchmark.json").read_text())
                models = execution["cpu_models"]
                if (
                    len(models) != 1
                    or not models[0].startswith("Intel(R) Xeon(R) CPU E5-2690 v4")
                    or not execution["host"].startswith("node-")
                    or not execution["nomultithread"]
                    or not execution["affinity"]
                    or execution["memory_limit"] != "7168 MiB"
                ):
                    raise ValueError(f"mismatched timing hardware/resources: {shard}")
                manifests[name].update(metadata["inputs"])
                identity = {
                    "solver": metadata["solver"],
                    "args": metadata["solver_args"],
                }
                if identity["args"] != (["--quiet"] if name == "primo" else []):
                    raise ValueError(f"unexpected solver arguments: {name}")
                if identities[name] and identity != identities[name][0]:
                    raise ValueError(f"mixed solver identities: {name}")
                identities[name].append(identity)
                if name == "primo" and profiles[name]["models"]["fully_validated"]:
                    with (shard / "answers.csv").open() as stream:
                        audit_models(shard, metadata, list(csv.DictReader(stream)))
            except (ValueError, KeyError, OSError) as error:
                issues.append(str(error))
        if profiles[name]["timing_complete"] and (
            profiles[name]["complete_shards"] != shards
            or len(identities[name]) != shards
        ):
            issues.append(f"unexpected shard count: {name}")
    timing_complete = all(profile["timing_complete"] for profile in profiles.values())
    if timing_complete and any(
        manifests[name] != manifests["primo"] for name in PROFILES
    ):
        issues.append("solver arms do not have identical input hashes")
    comparisons = {}
    for left, right in itertools.combinations(PROFILES, 2):
        common = raw[left].keys() & raw[right].keys()
        paired = [
            name
            for name in common
            if decisive(raw[left][name]) and decisive(raw[right][name])
        ]
        comparisons[f"{left}/{right}"] = {
            "common_rows": len(common),
            "jointly_decisive": len(paired),
            "disagreements": sorted(
                name
                for name in paired
                if raw[left][name]["answer"] != raw[right][name]["answer"]
            ),
        }
    clean = not issues and not any(
        profile["wrong"] or profile["exception_rows"] for profile in profiles.values()
    )
    clean &= not any(pair["disagreements"] for pair in comparisons.values())
    if timing_complete and clean:
        for name in PROFILES:
            directory = plot_data / name
            directory.mkdir(parents=True, exist_ok=True)
            rows = [raw[name][filename] for filename in sorted(raw[name])]
            with (directory / "shard-0.csv").open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
    return {
        "expected_instances": expected,
        "shards_per_solver": shards,
        "timeout_seconds": timeout,
        "timing_complete": timing_complete,
        "fully_qualified": timing_complete
        and clean
        and profiles["primo"]["models"]["fully_validated"],
        "identity_issues": issues,
        "profiles": profiles,
        "comparisons": comparisons,
        "identities": {
            name: values[0] if values else None for name, values in identities.items()
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--expected", type=int, default=519)
    parser.add_argument("--shards", type=int, default=64)
    parser.add_argument("--timeout", type=float, default=1200)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--plot-data", type=Path, required=True)
    parser.add_argument("--allow-incomplete", action="store_true")
    args = parser.parse_args()
    result = collect(
        args.root, args.expected, args.shards, args.timeout, args.plot_data
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    for name, profile in result["profiles"].items():
        print(
            name,
            profile["rows"],
            "rows;",
            profile["complete_shards"],
            "complete shards;",
            profile["solved"],
            "solved;",
            profile["models"]["completed"],
            "models",
        )
    print(
        "fully qualified:",
        result["fully_qualified"],
        "identity issues:",
        result["identity_issues"],
    )
    return 0 if result["fully_qualified"] or args.allow_incomplete else 1


if __name__ == "__main__":
    raise SystemExit(main())
