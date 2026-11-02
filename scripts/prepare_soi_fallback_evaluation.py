#!/usr/bin/env python3
"""Freeze eager/suspended Bland suffix comparisons without retuning SOI pricing."""

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
    args = parser.parse_args()
    base_path = Path("benchmarks/qf-lra-simplex-ablation/soi-full-plan.json")
    base = json.loads(base_path.read_text())
    previous = base["binaries"]["candidate"]
    plan = copy.deepcopy(base)
    configs = []
    combined = next(
        c["args"] for c in base["configurations"] if c["id"] == "final-g1-f0"
    )
    historical = ["--lra-pivoting-rule=soi"]
    for fixed in (0, 1):
        suffix = ["--lra-fixed-elimination"] if fixed else []
        for name, native_args in [("historical", historical), ("combined", combined)]:
            for arm, binary, extra in [
                ("original", "original", []),
                ("eager", "candidate", ["--no-lra-soi-suspend-on-fallback"]),
                ("suspended", "candidate", ["--lra-soi-suspend-on-fallback"]),
            ]:
                configs.append(
                    {
                        "id": f"{arm}-{name}-g1-f{fixed}",
                        "binary": binary,
                        "args": [*native_args, *extra, *suffix],
                    }
                )
        for arm, binary in [("original", "original"), ("current", "candidate")]:
            configs.append(
                {
                    "id": f"{arm}-sparse-g1-f{fixed}",
                    "binary": binary,
                    "args": ["--lra-pivoting-rule=sparse", *suffix],
                }
            )
    assert len(configs) == 16 and len({c["id"] for c in configs}) == 16
    plan.update(
        experiment="soi-bland-fallback-lifecycle-20261102",
        configurations=configs,
        aliases={},
        solver_sha256=args.candidate_sha256,
        solver_source=args.candidate_source,
        binaries={
            "original": previous,
            "candidate": {
                "sha256": args.candidate_sha256,
                "source": args.candidate_source,
            },
        },
        baseline="original-historical-g1-f0",
        previous_plan_sha256=digest(base_path),
    )
    plan["hypothesis"] = (
        "One pre-suffix snapshot plus suspended objective/activity/individual trailing reduces Bland-tail cost; rebuild at check boundaries preserves ordinary feasibility and rollback."
    )
    plan["acceptance"] = (
        "Zero hard correctness failures, complete focused/check/ASAN/fuzz and separate native+Dolmen SAT replay coverage. Compare stable solves and parent-bootstrap process PAR2 against eager same-binary and immutable previous profiles. Preserve selector defaults and report snapshot/rebuild cost. No pricing or stall retuning."
    )
    plan["stages"] = (
        "First use the previously frozen 100-input pilot (all16 configurations, three repeats); proceed to all1000 if non-regressing. Separate diagnostic profile repeats1; SAT-model replay never enters speed measurements."
    )
    write_json(args.out, plan)


if __name__ == "__main__":
    main()
