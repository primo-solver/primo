#!/usr/bin/env python3
"""Run Primo on one or more SMT-LIB files."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def classify(stdout: str, stderr: str, returncode: int) -> str:
    text = (stdout + "\n" + stderr).lower()
    if returncode == 124:
        return "TIMEOUT"
    if "unsupported" in text:
        return "UNSUPPORTED_INPUT"
    if returncode != 0:
        return "CRASH"
    for result in ("sat", "unsat", "unknown"):
        if stdout.strip().splitlines()[:1] == [result]:
            return result
    return "UNKNOWN_OUTPUT"


def run_file(path: Path, solver: str, timeout: float) -> tuple[str, int]:
    try:
        proc = subprocess.run(
            [solver, str(path)],
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
        status = classify(proc.stdout, proc.stderr, proc.returncode)
        print(f"{path}: {status}")
        if proc.stderr.strip():
            print(proc.stderr.strip(), file=sys.stderr)
        return status, proc.returncode
    except subprocess.TimeoutExpired:
        print(f"{path}: TIMEOUT")
        return "TIMEOUT", 124
    except FileNotFoundError:
        print(f"solver not found: {solver}", file=sys.stderr)
        return "CRASH", 127


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    hard_failure = False
    for path in args.files:
        status, returncode = run_file(path, args.solver, args.timeout)
        if status == "CRASH" and returncode == 127:
            hard_failure = True
    return 1 if hard_failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
