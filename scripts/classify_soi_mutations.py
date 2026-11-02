#!/usr/bin/env python3
"""Independently classify retained real-seed mutants; reject untriaged errors."""

from __future__ import annotations

import argparse
import json
import subprocess
from collections import Counter
from pathlib import Path

from benchmark_model_replay import digest, write_json


def run(command, source, timeout):
    try:
        result = subprocess.run(
            [*command, str(source)],
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return {"timeout": True}
    return {"exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr}


def answer(run):
    return (
        run["stdout"].strip()
        if run.get("exit") == 0 and run["stdout"].strip() in ("sat", "unsat")
        else None
    )


def classify(native, reference):
    if native.get("exit", 0) < 0 or reference.get("exit", 0) < 0:
        return "CRASH"
    if native.get("timeout") or reference.get("timeout"):
        return "TIMEOUT"
    expected = answer(reference)
    if expected:
        if answer(native):
            return "PASS" if answer(native) == expected else "WRONG_ANSWER"
        return "VALID_INPUT_NATIVE_ERROR"
    # type-aware mutation can replace a constant factor inside a seed's
    # multiplication, even when '*' is excluded from generative signatures.
    if (
        reference.get("exit") == 1
        and "logic does not support nonlinear arithmetic" in reference["stdout"]
    ):
        return "INVALID_GENERATED_NONLINEAR"
    return "UNTRIAGED_REFERENCE_ERROR"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutants", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=5)
    args = parser.parse_args()
    plan = json.loads(args.plan.read_text())
    config = next(c for c in plan["configurations"] if c["id"] == "final-g1-f0")
    native = [
        str(args.solver.resolve()),
        *plan["common_args"],
        *config["args"],
        "--check-model",
    ]
    # Stats are unrelated to the differential answer and kept out of stdout.
    reference = [str(args.reference.resolve()), "model_validate=true"]
    rows, counts = [], Counter()
    for path in sorted(args.mutants.glob("*.smt2")):
        n, r = run(native, path, args.timeout), run(reference, path, args.timeout)
        outcome = classify(n, r)
        counts[outcome] += 1
        rows.append(
            {
                "file": str(path),
                "sha256": digest(path),
                "native": n,
                "reference": r,
                "outcome": outcome,
            }
        )
        write_json(
            args.out,
            {
                "native_command": native,
                "reference_command": reference,
                "solver_sha256": digest(args.solver),
                "reference_sha256": digest(args.reference),
                "plan_sha256": digest(args.plan),
                "timeout_seconds": args.timeout,
                "counts": dict(counts),
                "rows": rows,
            },
        )
    if not rows or set(counts) - {"PASS", "TIMEOUT", "INVALID_GENERATED_NONLINEAR"}:
        raise SystemExit("mutation errors require triage")


if __name__ == "__main__":
    main()
