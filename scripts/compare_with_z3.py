#!/usr/bin/env python3
"""Compare Primo with reference SMT solvers."""

from __future__ import annotations

import argparse
import csv
import shutil
import subprocess
from pathlib import Path

RESULTS = {"sat", "unsat", "unknown"}


def run_solver(command: list[str], timeout: float) -> tuple[str, int, str, str]:
    try:
        proc = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", 124, "", ""
    except FileNotFoundError:
        return "MISSING", 127, "", ""

    first = proc.stdout.strip().splitlines()[0] if proc.stdout.strip() else ""
    text = (proc.stdout + "\n" + proc.stderr).lower()
    if "unsupported" in text:
        return "UNSUPPORTED_INPUT", proc.returncode, proc.stdout, proc.stderr
    if first in RESULTS:
        return first, proc.returncode, proc.stdout, proc.stderr
    if proc.returncode != 0:
        return "CRASH", proc.returncode, proc.stdout, proc.stderr
    return "UNKNOWN_OUTPUT", proc.returncode, proc.stdout, proc.stderr


def classify(ours: str, refs: list[str]) -> str:
    usable_refs = [r for r in refs if r not in {"MISSING", "TIMEOUT", "CRASH"}]
    concrete_refs = [r for r in usable_refs if r in {"sat", "unsat"}]
    if ours == "UNSUPPORTED_INPUT":
        return "UNSUPPORTED_INPUT"
    if ours == "CRASH":
        return "CRASH"
    if ours == "TIMEOUT":
        return "TIMEOUT"
    if len(set(concrete_refs)) > 1:
        return "REF_DISAGREEMENT"
    if not concrete_refs or ours == "unknown" or "unknown" in usable_refs:
        return "UNKNOWN_INVOLVED"
    ref = concrete_refs[0]
    if ours == ref:
        return "PASS"
    if ours == "sat" and ref == "unsat":
        return "OUR_SAT_REF_UNSAT"
    if ours == "unsat" and ref == "sat":
        return "OUR_UNSAT_REF_SAT"
    return "UNKNOWN_INVOLVED"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--refs", nargs="+", default=["z3", "cvc5"])
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out", type=Path, default=Path("results/compare.csv"))
    parser.add_argument("--expect-disagreement", action="store_true")
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for path in args.files:
        ours, _, _, _ = run_solver([args.solver, str(path)], args.timeout)
        ref_results = []
        for ref in args.refs:
            if shutil.which(ref) is None:
                ref_results.append("MISSING")
                continue
            ref_results.append(run_solver([ref, str(path)], args.timeout)[0])
        outcome = classify(ours, ref_results)
        rows.append(
            {
                "file": str(path),
                "ours": ours,
                "refs": " ".join(ref_results),
                "outcome": outcome,
            }
        )
        print(f"{path}: {outcome} ours={ours} refs={' '.join(ref_results)}")

    with args.out.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["file", "ours", "refs", "outcome"])
        writer.writeheader()
        writer.writerows(rows)

    if args.expect_disagreement:
        return (
            0
            if any(row["outcome"] not in {"PASS", "UNKNOWN_INVOLVED"} for row in rows)
            else 1
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
