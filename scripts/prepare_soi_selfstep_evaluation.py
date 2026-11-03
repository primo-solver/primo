#!/usr/bin/env python3
"""Freeze a paired self-event fix evaluation without retuning fallback policy."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path

from benchmark_model_replay import digest, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-sha256", required=True)
    parser.add_argument("--candidate-source", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--pilot", action="store_true")
    args = parser.parse_args()
    base_path = Path("benchmarks/qf-lra-simplex-ablation/soi-fallback-plan.json")
    base = json.loads(base_path.read_text())
    plan = copy.deepcopy(base)
    combined = next(
        row["args"]
        for row in base["configurations"]
        if row["id"] == "suspended-combined-g1-f0"
    )
    configurations = []
    for fixed in (0, 1):
        suffix = ["--lra-fixed-elimination"] if fixed else []
        variants = [
            ("old-combined", "original", combined),
            ("fixed-combined", "candidate", combined),
            ("sparse", "candidate", ["--lra-pivoting-rule=sparse"]),
            ("dantzig", "candidate", ["--lra-pivoting-rule=dantzig"]),
        ]
        if args.pilot:
            paper = ["--lra-pivoting-rule=soi", "--lra-soi-selection=paper"]
            variants += [
                ("old-paper", "original", paper),
                ("fixed-paper", "candidate", paper),
            ]
        for name, binary, options in variants:
            configurations.append(
                {
                    "id": f"{name}-g1-f{fixed}",
                    "binary": binary,
                    "args": [*options, *suffix],
                }
            )
    if args.pilot:
        pilot_path = Path(
            "benchmarks/qf-lra-simplex-ablation/soi-fallback-pilot-plan.json"
        )
        plan["selected_files"] = json.loads(pilot_path.read_text())["selected_files"]
        plan["inputs"] = len(plan["selected_files"])
        plan["pilot_selection_sha256"] = digest(pilot_path)
    plan.update(
        experiment="soi-self-zero-event-fix-20261103",
        configurations=configurations,
        baseline="old-combined-g1-f0",
        aliases={},
        solver_sha256=args.candidate_sha256,
        solver_source=args.candidate_source,
        binaries={
            "original": base["binaries"]["candidate"],
            "candidate": {
                "sha256": args.candidate_sha256,
                "source": args.candidate_source,
            },
        },
        previous_plan_sha256=digest(base_path),
        hypothesis="Excluding exact-zero own-bound events prevents repeated self-updates while retaining actual degenerate pivots and positive own-bound steps.",
        acceptance="Zero hard correctness failures;focused/check/ASAN/reference fuzz and real-seed mutation qualification;separate native+Dolmen validation of every successful SAT timing row. Keep Sparse and selector defaults unchanged. Report the repaired combined profile against immutable8faa775;no fallback or pricing retuning.",
        stages="Frozen100 pilot first,including old/fixed reference-paper controls;then frozen1000 paired8-profile evaluation,3 repeats,15s,Gaussian on,fixed off/on. Separate1-repeat profiles;model replay after timing. No changes to64/8 pricing/stall parameters or ordinary fallback handoff.",
    )
    write_json(args.out, plan)


if __name__ == "__main__":
    main()
