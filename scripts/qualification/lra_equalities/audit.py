#!/usr/bin/env python3
"""Audit the exact equality portfolio arms and summarize complementary solves."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from campaign import PROFILES


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--shards", type=int, default=32)
    parser.add_argument("--require-models", action="store_true")
    parser.add_argument("--binary-sha256", required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lra_ablation"))
    import summarize
    import audit_gates
    from run import rows

    summarize.PROFILES = PROFILES
    audit_gates.PROFILES = PROFILES
    report = summarize.collect(args.root / "full30", 1753, args.shards, 30,
                               args.require_models)
    if report["binary_sha256"] != args.binary_sha256:
        raise ValueError("unexpected timing binary")
    report["generated_gates"] = audit_gates.audit(args.root, args.binary_sha256,
                                                 generated_only=True)
    solved = {
        name: {row["file"] for row in rows(args.root / "full30/plot-data" / f"{name}.csv")
               if summarize.decisive(row)}
        for name, _ in PROFILES
    }
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
    output = args.root / ("qualified-audit.json" if args.require_models else "timing-audit.json")
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"profiles": report["profiles"], "portfolio": report["portfolio"]}, indent=2))


if __name__ == "__main__":
    main()
