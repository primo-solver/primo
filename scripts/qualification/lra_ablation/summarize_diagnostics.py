#!/usr/bin/env python3
"""Audit bounded diagnostic coverage and summarize counters and stack samples."""

from __future__ import annotations

import argparse
import collections
import csv
import json
from pathlib import Path
import re

from run import PROFILES, digest, write


def load(path: Path) -> dict:
    return json.loads(path.read_text())


def audit_result(path: Path, selected: dict, expected: list[str]) -> dict:
    result = load(path)
    if (result["command"][1:] != expected
            or (result["exit_code"] != 0 and not result["hard_killed"])
            or (result["answer"] in {"sat", "unsat"}
                and result["answer"] != selected["expected"])):
        raise ValueError(f"counter command, exit or answer mismatch: {path}")
    for stream in ("stdout", "stderr"):
        if digest(path.parent / stream) != result[f"{stream}_sha256"]:
            raise ValueError(f"changed output: {path}")
    observed = {key: int(value) for key, value in re.findall(
        r"^(:\S+) (\d+)$", (path.parent / "stderr").read_text(), re.MULTILINE)}
    if observed != result["statistics"] or (not result["timeout"] and not observed):
        raise ValueError(f"counter summary disagrees with raw statistics: {path}")
    return result


def audit_task(root: Path, mode: str, task: int, plan: dict, selected: dict,
               profiles: list[str], plan_path: Path) -> Path:
    base = root / mode / f"input-{task}"
    execution = load(base / "execution.json")
    if (execution["input"] != selected
            or execution["binary_sha256"] != plan["binary_sha256"]
            or execution["plan_sha256"] != digest(plan_path)
            or execution["cpu_models"] != ["Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz"]
            or execution["memory_mib"] != "7168"
            or execution["mode"] != mode or execution["array_task"] != task
            or not 1 <= len(execution["affinity"]) <= 2
            or "git: 4200525\n" not in execution["version"]
            or "cadical-3.0.0-7b99c07" not in execution["version"]
            or sorted(load(base / "complete.json")["profiles"]) != sorted(profiles)):
        raise ValueError(f"diagnostic provenance mismatch: {base}")
    return base


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--sensitivity-plan", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--followup-plan", type=Path)
    parser.add_argument("--tables", type=Path, help="Write compact durable CSV evidence here")
    args = parser.parse_args()
    plan = load(args.plan)
    sensitivity = load(args.sensitivity_plan)
    report: dict = {"inputs": [], "profiles": [], "sensitivity": [],
                    "qualification": "Diagnostics only; no replacement speed or model claims."}
    profiles = dict(PROFILES)
    counters = collections.Counter()
    for task, selected in enumerate(plan["inputs"]):
        base = audit_task(args.root, "counters", task, plan, selected,
                          list(profiles), args.plan)
        row = {"input": selected, "profiles": {}}
        for name, arguments in profiles.items():
            path = base / name / "summary.json"
            expected = [*arguments, "--stats", "--cancel-grace-seconds", "8",
                        "tmp/QF_LRA/" + selected["file"]]
            result = audit_result(path, selected, expected)
            counters["counter_runs"] += 1
            counters["timeouts"] += result["timeout"]
            counters["missing_statistics"] += not bool(result["statistics"])
            row["profiles"][name] = result
        report["inputs"].append(row)
    profile_plans = [(args.root, plan, args.plan)]
    if args.followup_plan:
        profile_plans.append((args.root / "followup", load(args.followup_plan), args.followup_plan))
    for profile_root, profile_plan, profile_path in profile_plans:
        for task in range(4 * len(profile_plan["profile_inputs"])):
            index, profile = divmod(task, 4)
            name = list(profiles)[profile]
            selected = profile_plan["inputs"][profile_plan["profile_inputs"][index]]
            base = audit_task(profile_root, "profile", task, profile_plan, selected, [name], profile_path)
            summary = load(base / name / "summary.json")
            rows = [json.loads(line) for line in (base / name / "samples.jsonl").read_text().splitlines()]
            if (summary["executable_sha256"] != profile_plan["binary_sha256"]
                    or summary["command"][1:] != [*profiles[name], "--stats",
                                                  "--cancel-grace-seconds", "8",
                                                  "tmp/QF_LRA/" + selected["file"]]
                    or len(rows) != summary["samples"]
                    or (summary["status"] != "complete"
                        and summary["diagnostic"] != "profile deadline exceeded")):
                raise ValueError(f"untriaged profiler failure: {base}")
            inclusive = collections.Counter(f for row in rows for f in set(row["frames"]))
            categories = {
                key: sum(any(key in frame for frame in row["frames"]) for row in rows)
                for key in ("LraTableau::pivot(", "LraTableau::check(", "LraTableau::pop(",
                            "refresh_model_phases(", "eliminate_monotone_variables(",
                            "__gmp", "CaDiCaL::Internal::analyze(", "CaDiCaL::Internal::propagate(",
                            "scan_for_propagations(", "reconstruct_monotone", "normalize_atom(")
            }
            report["profiles"].append({"input": selected, "profile": name, "summary": summary,
                                       "inclusive_samples": dict(inclusive), "categories": categories})
    for task, case in enumerate(sensitivity["sensitivity"]):
        selected = sensitivity["inputs"][case["input"]]
        name = f"{case['profile']}-{case['rule']}"
        base = audit_task(args.root, "sensitivity", task, sensitivity, selected,
                          [name], args.sensitivity_plan)
        expected = [*profiles[case["profile"]], f"--lra-pivoting-rule={case['rule']}",
                    "--stats", "--cancel-grace-seconds", "8", "tmp/QF_LRA/" + selected["file"]]
        result = audit_result(base / name / "summary.json", selected, expected)
        report["sensitivity"].append({"input": selected, "case": case, "result": result})
    report["counts"] = dict(counters)
    report["artifact_sha256"] = {
        str(path.relative_to(args.root)): digest(path)
        for path in sorted(args.root.rglob("*")) if path.is_file()
    }
    write(args.out, report)
    if args.tables:
        args.tables.mkdir(parents=True, exist_ok=True)
        metrics = [":sat.conflicts", ":theory.conflicts", ":sat.decisions",
                   ":sat.propagations", ":lra.online-check-calls", ":lra.pivots",
                   ":lra.pivot-term-ops", ":lra.rows", ":lra.tableau-vars",
                   ":lra.row-nonzeros", ":rational.big-operations",
                   ":lra.model-phase.refreshes", ":lra.model-phase.hints",
                   ":preprocessing.monotone-elimination.variables",
                   ":preprocessing.monotone-elimination.atoms-folded",
                   ":time.tactic.eliminate-monotone-us"]
        with (args.tables / "counters.csv").open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["file", "sha256", "profile", "answer", "timeout",
                             "diagnostic_seconds", "statistics_present", *metrics])
            for row in report["inputs"]:
                for name, result in row["profiles"].items():
                    writer.writerow([row["input"]["file"], row["input"]["sha256"], name,
                                     result["answer"], result["timeout"], result["seconds"],
                                     bool(result["statistics"]),
                                     *(result["statistics"].get(key) for key in metrics)])
        with (args.tables / "profiles.csv").open("w", newline="") as stream:
            writer = csv.writer(stream)
            keys = sorted(report["profiles"][0]["categories"])
            writer.writerow(["file", "sha256", "profile", "status", "samples", *keys])
            for row in report["profiles"]:
                writer.writerow([row["input"]["file"], row["input"]["sha256"],
                                 row["profile"], row["summary"]["status"], row["summary"]["samples"],
                                 *(row["categories"][key] for key in keys)])
        with (args.tables / "sensitivity.csv").open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["file", "profile", "rule", "answer", "timeout",
                             "diagnostic_seconds", "statistics_present"])
            for row in report["sensitivity"]:
                result = row["result"]
                writer.writerow([row["input"]["file"], row["case"]["profile"],
                                 row["case"]["rule"], result["answer"], result["timeout"],
                                 result["seconds"], bool(result["statistics"])])
    print(json.dumps(report["counts"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
