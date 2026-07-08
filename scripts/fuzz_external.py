#!/usr/bin/env python3
"""Wrap external SMT-LIB fuzzers behind one interface."""

from __future__ import annotations

import argparse
import csv
import random
import shutil
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fuzzer", choices=["yinyang", "fuzzsmt", "storm"], required=True)
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--refs", nargs="+", default=["z3", "cvc5"])
    parser.add_argument("--logic", default="QF_UF")
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    random.seed(args.seed)
    summary = args.out / "summary.csv"

    fuzzer_path = shutil.which(args.fuzzer)
    status = "SKIPPED_MISSING_FUZZER" if fuzzer_path is None else "TODO_FUZZER_WRAPPER"
    with summary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["fuzzer", "logic", "count", "seed", "status"])
        writer.writeheader()
        writer.writerow({"fuzzer": args.fuzzer, "logic": args.logic, "count": args.count, "seed": args.seed, "status": status})

    if fuzzer_path is None:
        print(f"{args.fuzzer} is not installed; skipping external fuzzing.")
    else:
        print(f"{args.fuzzer} found at {fuzzer_path}; wrapper implementation is TODO.")
    print(f"summary: {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
