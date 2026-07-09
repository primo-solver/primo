#!/usr/bin/env python3
"""Wrap external SMT-LIB fuzzers behind one interface."""

from __future__ import annotations

import argparse
import csv
import shlex
import subprocess
import random
import shutil
from pathlib import Path


RESULTS = {"sat", "unsat", "unknown"}


def run_solver(command: list[str], path: Path, timeout: float) -> tuple[str, int]:
    try:
        proc = subprocess.run(
            [*command, str(path)],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", 124
    except FileNotFoundError:
        return "MISSING", 127

    stdout = proc.stdout.strip()
    first = stdout.splitlines()[0] if stdout else ""
    text = (proc.stdout + "\n" + proc.stderr).lower()
    if "unsupported" in text:
        return "UNSUPPORTED_INPUT", proc.returncode
    if first in RESULTS:
        return first, proc.returncode
    if proc.returncode != 0:
        return "CRASH", proc.returncode
    return "UNKNOWN_OUTPUT", proc.returncode


def classify(ours: str, refs: list[str]) -> str:
    concrete_refs = [result for result in refs if result in {"sat", "unsat"}]
    if ours in {"CRASH", "TIMEOUT", "UNSUPPORTED_INPUT", "UNKNOWN_OUTPUT"}:
        return ours
    if len(set(concrete_refs)) > 1:
        return "REF_DISAGREEMENT"
    if not concrete_refs or ours == "unknown" or "unknown" in refs:
        return "UNKNOWN_INVOLVED"
    ref = concrete_refs[0]
    if ours == ref:
        return "PASS"
    if ours == "sat" and ref == "unsat":
        return "OUR_SAT_REF_UNSAT"
    if ours == "unsat" and ref == "sat":
        return "OUR_UNSAT_REF_SAT"
    return "UNKNOWN_INVOLVED"


def generate_fuzzsmt(
    fuzzer: str,
    logic: str,
    count: int,
    seed: int,
    out: Path,
    fuzzer_args: list[str],
) -> list[Path]:
    generated = out / "generated"
    generated.mkdir(parents=True, exist_ok=True)
    prefix = f"fuzzsmt_{seed}_"
    for stale in generated.glob(f"{prefix}*.smt2"):
        stale.unlink()
    command = [
        fuzzer,
        logic,
        "-seed",
        str(seed),
        *fuzzer_args,
        "-bulk-prefix",
        prefix,
        "-bulk-export",
        str(count),
    ]
    subprocess.run(command, cwd=generated, check=True)
    return sorted(generated.glob(f"{prefix}*.smt2"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fuzzer", choices=["yinyang", "fuzzsmt", "storm"], required=True)
    parser.add_argument("--seeds", type=Path)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--refs", nargs="+", default=["z3", "cvc5"])
    parser.add_argument("--logic", default="QF_UF")
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--fuzzer-args", default="", help="Extra arguments passed to the selected fuzzer")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    random.seed(args.seed)
    summary = args.out / "summary.csv"

    fuzzer_path = shutil.which(args.fuzzer)
    if fuzzer_path is None:
        with summary.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["fuzzer", "logic", "count", "seed", "status"])
            writer.writeheader()
            writer.writerow(
                {
                    "fuzzer": args.fuzzer,
                    "logic": args.logic,
                    "count": args.count,
                    "seed": args.seed,
                    "status": "SKIPPED_MISSING_FUZZER",
                },
            )
        print(f"{args.fuzzer} is not installed; skipping external fuzzing.")
        print(f"summary: {summary}")
        return 0

    if args.fuzzer != "fuzzsmt":
        status = "TODO_FUZZER_WRAPPER"
        with summary.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["fuzzer", "logic", "count", "seed", "status"])
            writer.writeheader()
            writer.writerow({"fuzzer": args.fuzzer, "logic": args.logic, "count": args.count, "seed": args.seed, "status": status})
        print(f"{args.fuzzer} found at {fuzzer_path}; wrapper implementation is TODO.")
        print(f"summary: {summary}")
        return 0

    files = generate_fuzzsmt(
        fuzzer_path,
        args.logic,
        args.count,
        args.seed,
        args.out,
        shlex.split(args.fuzzer_args),
    )
    solver_command = shlex.split(args.solver)
    ref_commands = [shlex.split(ref) for ref in args.refs if shutil.which(shlex.split(ref)[0]) is not None]
    bugs = args.out / "bugs"
    bugs.mkdir(parents=True, exist_ok=True)

    rows = []
    for path in files:
        ours, _ = run_solver(solver_command, path, args.timeout)
        ref_results = [run_solver(ref, path, args.timeout)[0] for ref in ref_commands]
        outcome = classify(ours, ref_results)
        rows.append({"file": str(path), "ours": ours, "refs": " ".join(ref_results), "outcome": outcome})
        if outcome not in {"PASS", "UNKNOWN_INVOLVED"}:
            shutil.copy2(path, bugs / path.name)
        print(f"{path}: {outcome} ours={ours} refs={' '.join(ref_results)}")

    with summary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["file", "ours", "refs", "outcome"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"summary: {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
