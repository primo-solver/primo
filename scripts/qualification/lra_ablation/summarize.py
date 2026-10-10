#!/usr/bin/env python3
"""Audit four matched ablation arms and their separately recorded SAT models."""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
import statistics
from pathlib import Path

from run import PROFILES, digest, rows, write


def decisive(row: dict[str, str]) -> bool:
    return (row["status"] == "ok" and row["answer"] in {"sat", "unsat"}
            and row["exit_code"] == "0" and row["timeout"] == "false")


def audit_models(shard: Path, parent: dict, answers: list[dict[str, str]]) -> int:
    directory = shard / "model-validation"
    report = json.loads((directory / "coverage.json").read_text())
    if (report["status"] != "complete" or report["validator_exit"] != 0
            or not report["inputs_and_binary_unchanged"] or report["missing"]
            or report["malformed_rows"]):
        raise ValueError(f"incomplete or failed model qualification: {shard}")
    identity = report["provenance"]
    if (identity["parent_answers_sha256"] != digest(shard / "answers.csv")
            or identity["parent_provenance_sha256"] != digest(shard / "provenance.json")):
        raise ValueError(f"changed model parent: {shard}")
    for name, checksum in report["artifacts"].items():
        path = directory / name
        if checksum is None:
            if path.exists():
                raise ValueError(f"unexpected model artifact: {path}")
        elif not path.exists() or digest(path) != checksum:
            raise ValueError(f"missing or changed model artifact: {path}")
    expected = {r["file"] for r in answers if decisive(r) and r["answer"] == "sat"}
    summary = directory / "models/summary.csv"
    observed = rows(summary) if summary.exists() else []
    names = [str(Path(r["file"]).relative_to(parent["root"])) for r in observed]
    if (len(observed) != len(expected) or set(names) != expected
            or report["expected_sat"] != len(expected) or report["completed"] != len(expected)
            or report["dolmen_pass"] != len(expected)
            or any(r["outcome"] != "pass" or r["answer"] != "sat" or r["validator"] != "dolmen"
                   or r["solver_exit"] != "0" or r["dolmen_exit"] != "0" for r in observed)):
        raise ValueError(f"models do not cover every successful SAT row: {shard}")
    return len(expected)


def metrics(values: list[dict[str, str]], timeout: float) -> dict:
    return {
        "inputs": len(values), "solved": sum(decisive(r) for r in values),
        "sat": sum(decisive(r) and r["answer"] == "sat" for r in values),
        "timeouts": sum(r["status"] == "timeout" for r in values),
        "unknown": sum(r["status"] == "ok" and r["answer"] == "unknown" for r in values),
        "errors": sum(r["status"] not in {"ok", "timeout"} for r in values),
        "wrong": sum(r["wrong"] == "true" for r in values),
        "par2_sum": sum(float(r["seconds"]) if decisive(r) else 2 * timeout for r in values),
    }


def collect(root: Path, expected: int, shards: int, timeout: float, require_models: bool) -> dict:
    all_rows, all_hashes, binary_hashes, profile_reports = {}, {}, set(), {}
    for name, arguments in PROFILES:
        merged, inputs, model_count = {}, {}, 0
        for index in range(shards):
            shard = root / name / f"shard-{index}"
            parent = json.loads((shard / "provenance.json").read_text())
            execution = json.loads((shard / "execution-benchmark.json").read_text())
            if (parent["status"] != "complete" or not parent["inputs_unchanged"]
                    or parent["solver_args"] != arguments or parent["timeout"] != timeout
                    or parent["shards"] != shards or parent["shard_index"] != index
                    or parent["solver"]["sha256"] != execution["binary_sha256"]
                    or execution["solver_args"] != arguments or execution["memory_mib"] != "7168"
                    or execution["partition"] != "compute"
                    or len(execution["cpu_models"]) != 1
                    or not execution["cpu_models"][0].startswith("Intel(R) Xeon(R) CPU E5-2690 v4")):
                raise ValueError(f"invalid measurement provenance: {shard}")
            binary_hashes.add(parent["solver"]["sha256"])
            observed = rows(shard / "answers.csv")
            if len(observed) != len(parent["inputs"]) or {r["file"] for r in observed} != set(parent["inputs"]):
                raise ValueError(f"missing or duplicate shard inputs: {shard}")
            for row in observed:
                if row["file"] in merged:
                    raise ValueError(f"duplicate input across shards: {row['file']}")
                if row["wrong"] == "true" or row["status"] not in {"ok", "timeout"}:
                    raise ValueError(f"benchmark correctness failure: {row}")
                if row["status"] == "ok" and not decisive(row):
                    raise ValueError(f"unexpected successful-process outcome: {row}")
                merged[row["file"]] = row
            inputs.update(parent["inputs"])
            if require_models:
                model_count += audit_models(shard, parent, observed)
        if len(merged) != expected:
            raise ValueError(f"incomplete full profile: {name}: {len(merged)}/{expected}")
        all_rows[name], all_hashes[name] = merged, inputs
        profile_reports[name] = metrics(list(merged.values()), timeout)
        profile_reports[name]["direct_dolmen_models"] = model_count if require_models else None
        plot = root / "plot-data"
        plot.mkdir(exist_ok=True)
        with (plot / f"{name}.csv").open("w") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(next(iter(merged.values()))))
            writer.writeheader()
            writer.writerows(merged.values())
    if len(binary_hashes) != 1 or any(v != all_hashes["default"] for v in all_hashes.values()):
        raise ValueError("ablation arms have different binaries or inputs")
    for a, b in itertools.combinations(all_rows, 2):
        for name, row in all_rows[a].items():
            other = all_rows[b][name]
            if decisive(row) and decisive(other) and row["answer"] != other["answer"]:
                raise ValueError(f"profile answer disagreement: {a}/{b}: {name}")
    comparisons = {}
    for name in ("phase", "monotone", "both"):
        baseline, candidate = all_rows["default"], all_rows[name]
        gains = [n for n in baseline if not decisive(baseline[n]) and decisive(candidate[n])]
        losses = [n for n in baseline if decisive(baseline[n]) and not decisive(candidate[n])]
        ratios = [float(candidate[n]["seconds"]) / max(float(baseline[n]["seconds"]), 1e-9)
                  for n in baseline if decisive(baseline[n]) and decisive(candidate[n])]
        comparisons[name] = {"gains": gains, "losses": losses,
                             "median_common_runtime_ratio": statistics.median(ratios) if ratios else None,
                             "par2_ratio": profile_reports[name]["par2_sum"] / profile_reports["default"]["par2_sum"]}
    families = sorted({r["family"] for r in all_rows["default"].values()})
    return {
        "profiles": profile_reports, "comparisons": comparisons,
        "families": {family: {name: metrics([r for r in values.values() if r["family"] == family], timeout)
                              for name, values in all_rows.items()} for family in families},
        "binary_sha256": next(iter(binary_hashes)), "models_complete": require_models,
        "input_manifest_sha256": hashlib.sha256(json.dumps(all_hashes["default"], sort_keys=True).encode()).hexdigest(),
        "timeout_seconds": timeout, "inputs": expected, "shards_per_profile": shards,
        "decisive_disagreements": 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--expected", type=int, required=True)
    parser.add_argument("--shards", type=int, required=True)
    parser.add_argument("--timeout", type=float, required=True)
    parser.add_argument("--require-models", action="store_true")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = collect(args.root, args.expected, args.shards, args.timeout, args.require_models)
    write(args.out, report)
    print(json.dumps(report["profiles"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
