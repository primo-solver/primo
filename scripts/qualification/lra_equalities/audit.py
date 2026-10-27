#!/usr/bin/env python3
"""Audit the exact equality portfolio arms and summarize complementary solves."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from campaign import PROFILES


def retained_mutations(root: Path, binary: str, rows) -> dict:
    reports = {}
    identities_by_profile = {}
    for name, arguments in PROFILES:
        identities = {}
        count = 0
        for index in range(8):
            shard = root / "retained-mutant-replay" / name / f"shard-{index}"
            execution = json.loads((shard / "execution-mutants.json").read_text())
            if execution["binary_sha256"] != binary or execution["solver_args"] != arguments:
                raise ValueError("retained mutation binary/profile mismatch")
            selected = json.loads((shard / "input-identities.json").read_text())
            observed = rows(shard / "answers.csv")
            expected_aliases = {f"{i}.smt2" for i in range(len(selected))}
            if (len(observed) != len(selected)
                    or {Path(row["file"]).name for row in observed} != expected_aliases
                    or set(identities) & set(selected)):
                raise ValueError("retained mutation coverage mismatch")
            if any(row["outcome"] != "PASS" or row["ours_exit"] != "0"
                   or row["reference_exit"] != "0" or row["ours"] not in {"sat", "unsat"}
                   or row["ours"] != row["reference"] for row in observed):
                raise ValueError("retained mutation result needs triage")
            count += len(observed)
            identities.update(selected)
        if count == 0:
            raise ValueError("empty retained mutation campaign")
        identities_by_profile[name] = identities
        reports[name] = {"files": count, "unique_content_sha256": len(set(identities.values())),
                         "pass": count, "wrong": 0, "crash": 0, "timeout": 0,
                         "reference_disagreement": 0}
    if any(value != identities_by_profile["default"] for value in identities_by_profile.values()):
        raise ValueError("retained mutations differ across profiles")
    return reports


def retained_sessions(root: Path, binary: str) -> dict:
    reports = {}
    for profile, name in enumerate(("equality",)):
        totals = {"sessions": 0, "pass": 0, "queries": 0, "sat_models": 0}
        for shard in range(4):
            directory = root / "incremental" / f"shard-{2 * shard + profile}"
            run = json.loads((directory / "run.json").read_text())
            summary = json.loads((directory / "summary.json").read_text())
            arguments = run["arguments"]
            expected_args = ["--quiet", "--lra-equality-propagation"]
            expected_args += ["--incremental-mode=assumptions", "--max-theory-conflicts=10000"]
            if (run["solver_sha256"] != binary or arguments["solver_arg"] != expected_args
                    or arguments["count"] != 250 or arguments["kind"] != "generated"
                    or arguments["seed"] != 2026102150 + shard * 250
                    or summary["sessions"] != 250 or summary["pass"] != 250
                    or summary["failures"] or summary["untriaged_failure"]):
                raise ValueError("retained session gate incomplete or failed")
            for key in totals:
                totals[key] += summary[key]
        reports[name] = totals
    rejected = []
    for slot in (1, 3, 5, 7):
        directory = root / "incremental" / f"shard-{slot}"
        if not directory.exists():
            continue
        run = json.loads((directory / "run.json").read_text())
        summary = json.loads((directory / "summary.json").read_text())
        expected_error = '(error "assumptions mode rejects assertion-dependent preprocessing")'
        failures = summary["failures"]
        if (run["solver_sha256"] != binary
                or run["arguments"]["solver_arg"] != ["--quiet", "--lra-equality-propagation",
                    "--lra-model-phase", "--monotone-elimination", "--incremental-mode=assumptions",
                    "--max-theory-conflicts=10000"]
                or summary["sessions"] != 1 or summary["pass"] != 0 or len(failures) != 1
                or failures[0]["result"]["queries"][0]["answer"] != expected_error):
            raise ValueError("unclassified rejected incremental configuration")
        rejected.append(slot)
    reports["both-equality-assumptions"] = {
        "status": "unsupported-configuration", "rejected_attempts": rejected,
        "reason": "Existing assumptions-mode guard forbids monotone elimination; one-shot profile remains supported.",
    }
    return reports


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--shards", type=int, default=32)
    parser.add_argument("--require-models", action="store_true")
    parser.add_argument("--gates-only", action="store_true")
    parser.add_argument("--binary-sha256", required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lra_ablation"))
    import audit_gates
    import summarize
    from run import rows

    summarize.PROFILES = PROFILES
    audit_gates.PROFILES = PROFILES
    generated = audit_gates.audit(args.root, args.binary_sha256, generated_only=True)
    generated["status"] = "generated-qualified"
    for key in ("mutation_inputs", "invalid_mutation_inputs", "triage_artifact_sha256",
                "distinct_mutation_hashes"):
        generated.pop(key)
    for counts in generated["profiles"].values():
        for key in ("mutant_replays", "mutant_pass", "mutant_timeouts", "invalid_mutations_excluded"):
            counts.pop(key)
    gates = {"status": "qualified", "generated": generated,
             "retained_mutations": retained_mutations(args.root, args.binary_sha256, rows),
             "retained_sessions": retained_sessions(args.root, args.binary_sha256)}
    if args.gates_only:
        (args.root / "gate-audit.json").write_text(json.dumps(gates, indent=2) + "\n")
        print(json.dumps(gates, indent=2))
        return
    report = summarize.collect(args.root / "full30", 1753, args.shards, 30,
                               args.require_models)
    if report["binary_sha256"] != args.binary_sha256:
        raise ValueError("unexpected timing binary")
    report["correctness_gates"] = gates
    measurements = {name: rows(args.root / "full30/plot-data" / f"{name}.csv")
                    for name, _ in PROFILES}
    solved = {name: {row["file"] for row in observed if summarize.decisive(row)}
              for name, observed in measurements.items()}
    report["portfolio"] = {
        "all_arms_union": len(set().union(*solved.values())),
        "default_both_union": len(solved["default"] | solved["both"]),
        "new_beyond_default_both": sorted((solved["equality"] | solved["both-equality"])
                                          - (solved["default"] | solved["both"])),
        "equality_gains_over_default": sorted(solved["equality"] - solved["default"]),
        "equality_losses_against_default": sorted(solved["default"] - solved["equality"]),
        "both_equality_gains_over_both": sorted(solved["both-equality"] - solved["both"]),
        "both_equality_losses_against_both": sorted(solved["both"] - solved["both-equality"]),
    }
    # Exploratory censoring of these same measurements, not new timed portfolios.
    report["derived_cutoffs"] = []
    for cutoff in (0.1, 1, 3, 5, 10, 20, 30):
        covered = {
            name: {row["file"] for row in observed
                   if summarize.decisive(row) and float(row["seconds"]) <= cutoff}
            for name, observed in measurements.items()
        }
        baseline = covered["default"] | covered["both"]
        all_arms = set().union(*covered.values())
        report["derived_cutoffs"].append({
            "cutoff_seconds_per_configuration": cutoff,
            "default_both_union": len(baseline), "all_arms_union": len(all_arms),
            "additional_files": sorted(all_arms - baseline),
        })
    output = args.root / ("qualified-audit.json" if args.require_models else "timing-audit.json")
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"profiles": report["profiles"], "portfolio": report["portfolio"]}, indent=2))


if __name__ == "__main__":
    main()
