#!/usr/bin/env python3
"""Audit all repeated lost-solve pairs and their separate SAT-model coverage."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from run import PROFILES, digest, rows, write
from summarize import audit_models, decisive


def collect(root: Path) -> dict:
    profiles = {}
    artifacts = {}
    models = 0
    parents = 0
    options = dict(PROFILES)
    for path in sorted((root / "loss-plans").glob("*.json")):
        plan = json.loads(path.read_text())
        name = f"{plan['dataset']}-{plan['profile']}"
        if path.stem != name or plan["profile"] not in {"phase", "monotone", "both"}:
            raise ValueError("unexpected paired plan identity")
        if len({item["file"] for item in plan["inputs"]}) != len(plan["inputs"]):
            raise ValueError("duplicate paired inputs")
        artifacts[str(path.relative_to(root))] = digest(path)
        cases = []
        model_count = 0
        for index, item in enumerate(plan["inputs"]):
            case = root / "loss-repeats" / name / f"input-{index}"
            for phase in ("benchmark", "replay"):
                execution = json.loads((case / f"execution-{phase}.json").read_text())
                complete = json.loads((case / f"{phase}-complete.json").read_text())
                if (execution["array_task"] != index or execution["input"] != item
                        or execution["binary_sha256"] != plan["binary_sha256"]
                        or execution["plan_sha256"] != digest(path)
                        or execution["memory_mib"] != "7168" or execution["phase"] != phase
                        or execution["cpu_models"] != ["Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz"]
                        or complete != {"input": item, "repetitions": 3}):
                    raise ValueError("incomplete or changed paired execution")
            pairs = []
            for repetition in range(3):
                answers = {}
                for profile in ("default", plan["profile"]):
                    directory = case / f"round-{repetition}" / profile
                    parent = json.loads((directory / "provenance.json").read_text())
                    observed = rows(directory / "answers.csv")
                    if (parent["status"] != "complete" or not parent["inputs_unchanged"]
                            or parent["inputs"] != {item["file"]: item["sha256"]}
                            or parent["solver"]["sha256"] != plan["binary_sha256"]
                            or parent["solver_args"] != options[profile]
                            or parent["timeout"] != plan["timeout"]
                            or parent["shards"] != 1 or parent["shard_index"] != 0
                            or len(observed) != 1 or observed[0]["file"] != item["file"]):
                        raise ValueError("paired timing provenance or row coverage mismatch")
                    row = observed[0]
                    if (row["wrong"] != "false" or row["status"] not in {"ok", "timeout"}
                            or (row["status"] == "ok" and (row["exit_code"] != "0"
                                or row["timeout"] != "false"
                                or row["answer"] not in {"sat", "unsat", "unknown"}))):
                        raise ValueError("paired run has a correctness error")
                    model_count += audit_models(directory, parent, observed)
                    parents += 1
                    answers[profile] = row
                baseline, candidate = answers["default"], answers[plan["profile"]]
                if decisive(baseline) and decisive(candidate) and baseline["answer"] != candidate["answer"]:
                    raise ValueError("paired decisive answer disagreement")
                pairs.append({
                    "default_solved": decisive(baseline), "candidate_solved": decisive(candidate),
                    "default_seconds": float(baseline["seconds"]),
                    "candidate_seconds": float(candidate["seconds"]),
                    "lost": decisive(baseline) and not decisive(candidate),
                })
            cases.append({"input": item, "lost_pairs": sum(p["lost"] for p in pairs), "pairs": pairs})
        profiles[name] = {
            "inputs": len(cases), "repeated_losses_at_least_two_pairs": sum(c["lost_pairs"] >= 2 for c in cases),
            "losses_in_all_three_pairs": sum(c["lost_pairs"] == 3 for c in cases),
            "nonrepeated_original_losses": sum(c["lost_pairs"] < 2 for c in cases),
            "direct_dolmen_models": model_count, "cases": cases,
        }
        models += model_count
    if set(profiles) != {f"{d}-{p}" for d in ("full30", "selection1200")
                         for p in ("phase", "monotone", "both")}:
        raise ValueError("missing paired profiles")
    for path in (root / "loss-repeats").rglob("*"):
        if path.is_file() and path.name in {"answers.csv", "provenance.json", "coverage.json",
                                          "execution-benchmark.json", "execution-replay.json"}:
            artifacts[str(path.relative_to(root))] = digest(path)
    return {"status": "complete", "profiles": profiles, "timing_and_model_parents": parents,
            "direct_dolmen_models": models, "wrong_answers": 0, "invalid_models": 0,
            "decisive_disagreements": 0, "artifact_sha256": artifacts,
            "promotion": "Deferred: aggregate full-corpus gains coexist with repeatable performance losses."}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = collect(args.root)
    write(args.out, report)
    print(json.dumps({name: {k: v for k, v in data.items() if k != "cases"}
                      for name, data in report["profiles"].items()}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
