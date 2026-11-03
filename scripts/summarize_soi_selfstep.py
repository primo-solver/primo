#!/usr/bin/env python3
"""Keep portable paired SOI self-event results and same-setting comparisons."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from benchmark_model_replay import digest, write_json
from summarize_lra_simplex_ablation import clustered_difference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--qualification", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    raw = json.loads(args.summary.read_text())
    qualification = json.loads(args.qualification.read_text())
    plan = json.loads(args.plan.read_text())
    if qualification["models"]["passed"] != qualification["models"]["expected"]:
        raise ValueError("incomplete SAT model coverage")
    configurations = {c["id"] for c in plan["configurations"]}
    if configurations != set(raw["configurations"]):
        raise ValueError("summary configuration set differs from plan")
    comparisons = {}
    for fixed in (0, 1):
        candidate = f"fixed-combined-g1-f{fixed}"
        cases = raw["per_input"][candidate]
        for baseline in sorted(configurations):
            if baseline == candidate or not baseline.endswith(f"-f{fixed}"):
                continue
            original = raw["per_input"][baseline]
            if set(cases) != set(original):
                raise ValueError("paired input population differs")
            comparisons[f"{candidate}-vs-{baseline}"] = {
                "par2_change": clustered_difference(
                    [
                        (cases[n]["parent"], cases[n]["par2"] - original[n]["par2"])
                        for n in sorted(cases)
                    ],
                    20261102,
                ),
                "stable_solve_gains": sorted(
                    n for n in cases if cases[n]["stable"] and not original[n]["stable"]
                ),
                "stable_solve_losses": sorted(
                    n for n in cases if original[n]["stable"] and not cases[n]["stable"]
                ),
            }
    write_json(
        args.out,
        {
            "analysis_sha256": digest(Path(__file__)),
            "summary_sha256": digest(args.summary),
            "qualification_sha256": digest(args.qualification),
            "plan_sha256": digest(args.plan),
            "binaries": plan["binaries"],
            "inputs": plan["inputs"],
            "repeats": plan["repeats"],
            "timeout_seconds": plan["timeout_seconds"],
            "configurations": raw["configurations"],
            "comparisons": comparisons,
            "timing_machines": raw["timing_machines"],
            "qualification": qualification,
            "defaults": "Sparse solver default;historical SOI selector unchanged;combined experimental",
        },
    )


if __name__ == "__main__":
    main()
