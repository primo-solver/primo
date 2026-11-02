#!/usr/bin/env python3
"""Refute selected SOI reason conjunctions independently of Primo's tableau.

The fixture executable saves base equations before pivoting and expands its
propagated bound into original antecedents. A successful process exit and an
exact `unsat` answer are both required. This checks the focused fixtures, not
all theory lemmas emitted by the SMT engine.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def refute(reference: Path, problem: Path, timeout: float) -> dict:
    command = [str(reference), str(problem)]
    try:
        process = subprocess.run(
            command, capture_output=True, text=True, timeout=timeout, check=False
        )
    except subprocess.TimeoutExpired:
        return {"file": problem.name, "command": command, "status": "timeout"}
    answers = [line.strip() for line in process.stdout.splitlines() if line.strip()]
    passed = process.returncode == 0 and answers == ["unsat"]
    return {
        "file": problem.name,
        "sha256": hashlib.sha256(problem.read_bytes()).hexdigest(),
        "command": command,
        "status": "pass" if passed else "failure",
        "exit_code": process.returncode,
        "stdout": process.stdout,
        "stderr": process.stderr,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=5)
    args = parser.parse_args()
    files = sorted(args.fixtures.glob("*.smt2"))
    if not files:
        raise SystemExit("no SOI explanation fixtures found")
    rows = [refute(args.reference.resolve(), path, args.timeout) for path in files]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"rows": rows}, indent=2) + "\n")
    return int(any(row["status"] != "pass" for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
