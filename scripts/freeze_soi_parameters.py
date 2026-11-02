#!/usr/bin/env python3
"""Apply the predeclared SOI pilot rule without retuning on the full corpus."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from benchmark_model_replay import digest, write_json


def choose(frozen, report):
    baseline = report["configurations"]["paper-g1-f0"]
    if baseline["inputs"] != 100 or sum(baseline["outcomes"].values()) != 300:
        raise ValueError("pilot must contain all 100 inputs and three repetitions")
    candidates = []
    for budget in frozen["candidate_budgets"]:
        for stall in frozen["stall_limits"]:
            name = f"short-c{budget}-s{stall}-g1-f0"
            config = report["configurations"][name]
            comparison = report["comparisons"][f"{name}-vs-paper-g1-f0"]
            if config["inputs"] != 100 or sum(config["outcomes"].values()) != 300:
                raise ValueError("incomplete candidate pilot")
            if not comparison["stable_solve_losses"]:
                candidates.append((config["process_par2"], budget, stall, name))
    if not candidates:
        return {
            "budget": 16,
            "stall_limit": 16,
            "status": "fallback-experimental",
            "eligible": [],
        }
    score, budget, stall, name = min(candidates)
    assert re.fullmatch(r"short-c\d+-s\d+-g1-f0", name)
    return {
        "budget": budget,
        "stall_limit": stall,
        "status": "frozen-for-full-evaluation",
        "selected": name,
        "process_par2": score,
        "eligible": [row[3] for row in sorted(candidates)],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frozen", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--pilot-plan", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = choose(
        json.loads(args.frozen.read_text()), json.loads(args.summary.read_text())
    )
    result.update(
        frozen_selection_sha256=digest(args.frozen),
        pilot_summary_sha256=digest(args.summary),
        pilot_plan_sha256=digest(args.pilot_plan),
        rule="No lost stable solves against paper selection; minimum summed mean-repeat process PAR2; ties by budget then stall. No full-corpus evidence used.",
    )
    write_json(args.out, result)


if __name__ == "__main__":
    main()
