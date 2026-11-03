#!/usr/bin/env python3
"""Measure coverage complementarity from qualified SOI per-input results."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from benchmark_model_replay import digest, write_json


def analyze(summary, plan):
    cases = summary["per_input"]
    expected = set(plan["selected_files"])
    result = {}
    ordinary_all, soi_all = set(), set()
    for fixed in (0, 1):
        names = {
            rule: f"current-{rule}-g1-f{fixed}"
            for rule in ("sparse", "dantzig", "bland")
        }
        names["soi"] = f"suspended-combined-g1-f{fixed}"
        for cfg in names.values():
            if set(cases[cfg]) != expected:
                raise ValueError("configuration/input population mismatch")
        solved = {
            rule: {n for n, r in cases[cfg].items() if r["stable"]}
            for rule, cfg in names.items()
        }
        ordinary = solved["sparse"] | solved["dantzig"] | solved["bland"]
        ordinary_all.update(ordinary)
        soi_all.update(solved["soi"])
        pairs = {}
        for rule in ("sparse", "dantzig", "bland", "ordinary_union"):
            baseline = ordinary if rule == "ordinary_union" else solved[rule]
            added = sorted(solved["soi"] - baseline)
            pairs[rule] = {
                "baseline_stable": len(baseline),
                "soi_stable": len(solved["soi"]),
                "intersection": len(baseline & solved["soi"]),
                "soi_only": [
                    {"file": n, "family": cases[names["soi"]][n]["family"]}
                    for n in added
                ],
                "baseline_only_count": len(baseline - solved["soi"]),
                "union_stable": len(baseline | solved["soi"]),
            }
        base_scores, added_scores, wins = [], [], []
        for name in sorted(expected):
            soi = cases[names["soi"]][name]
            base = min(
                cases[names[r]][name]["par2"] for r in ("sparse", "dantzig", "bland")
            )
            base_scores.append(base)
            added_scores.append(min(base, soi["par2"]))
            times = [
                cases[names[r]][name]["seconds"]
                for r in ("sparse", "dantzig", "bland")
                if cases[names[r]][name]["stable"]
            ]
            if soi["stable"] and times and soi["seconds"] < min(times):
                wins.append(
                    {
                        "file": name,
                        "seconds_saved": min(times) - soi["seconds"],
                        "ratio": min(times) / soi["seconds"],
                    }
                )
        baseline_score, added_score = sum(base_scores), sum(added_scores)
        result[str(fixed)] = {
            "comparisons": pairs,
            "hindsight_selector": {
                "ordinary_par2": baseline_score,
                "ordinary_plus_soi_par2": added_score,
                "reduction_seconds": baseline_score - added_score,
                "reduction_percent": 100
                * (baseline_score - added_score)
                / baseline_score,
                "median_runtime_win_count": len(wins),
                "median_runtime_wins": wins,
                "max_median_speedup": max((w["ratio"] for w in wins), default=None),
                "max_median_seconds_saved": max(
                    (w["seconds_saved"] for w in wins), default=None
                ),
            },
        }
    result["across_fixed_settings"] = {
        "ordinary_union_stable": len(ordinary_all),
        "ordinary_plus_soi_union_stable": len(ordinary_all | soi_all),
        "soi_union_stable": len(soi_all),
        "soi_only": sorted(soi_all - ordinary_all),
        "uncovered": sorted(expected - ordinary_all - soi_all),
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--qualified-report", type=Path, required=True)
    parser.add_argument("--qualification", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.qualified_report.read_text())
    if report["raw_sha256"] != digest(args.summary):
        raise ValueError("raw summary differs from the qualified report")
    qualification = json.loads(args.qualification.read_text())
    if (
        qualification.get("qualification_status") != "complete"
        or qualification["models"]["expected"] != qualification["models"]["passed"]
    ):
        raise ValueError("incomplete SAT model qualification")
    summary = json.loads(args.summary.read_text())
    plan = json.loads(args.plan.read_text())
    write_json(
        args.out,
        {
            "schema": 1,
            "analysis_sha256": digest(Path(__file__)),
            "summary_sha256": digest(args.summary),
            "qualification_sha256": digest(args.qualification),
            "plan_sha256": digest(args.plan),
            "native": plan["binaries"]["candidate"],
            "inputs": plan["inputs"],
            "repeats": plan["repeats"],
            "timeout_seconds": plan["timeout_seconds"],
            "coverage_definition": "Stable means successful SAT/UNSAT in all three original repetitions. Set differences compare current combined suspended SOI with current ordinary policies; Gaussian on, fixed settings separately and together.",
            "selector_definition": "For each input take the minimum mean-repeat PAR2 among named profiles, then sum. Median-runtime wins are separately computed on stable solved overlaps. This is hindsight profile selection, not an executed parallel race, budget-sharing schedule, confidence interval or proven speedup.",
            "results": analyze(summary, plan),
        },
    )


if __name__ == "__main__":
    main()
