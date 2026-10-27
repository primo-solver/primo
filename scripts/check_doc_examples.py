#!/usr/bin/env python3
"""Check the manual's small examples and captured CLI help against a built Primo."""

from __future__ import annotations

import argparse
import difflib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "docs" / "examples"


def check_text(name: str, expected: str, actual: str) -> bool:
    if expected == actual:
        return True
    print(f"FAIL {name}")
    print("".join(difflib.unified_diff(
        expected.splitlines(keepends=True), actual.splitlines(keepends=True),
        fromfile="documented", tofile="actual",
    )), end="")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, default=ROOT / "build" / "primo")
    parser.add_argument("--timeout", type=float, default=10.0, help="Seconds per invocation")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    solver = str(args.solver.resolve())
    cases = json.loads((EXAMPLES / "cases.json").read_text())
    failed = 0
    for case in cases:
        command = [solver, "--quiet", *case["args"]]
        source = EXAMPLES / case["input"]
        input_text = source.read_text() if case.get("stdin", False) else None
        if input_text is None:
            command.append(str(source))
        result = subprocess.run(command, input=input_text, capture_output=True,
                                text=True, timeout=args.timeout, check=False)
        matches = check_text(case["name"], (EXAMPLES / case["expected"]).read_text(),
                             result.stdout)
        if result.returncode != case["exit_code"]:
            print(f"FAIL {case['name']}: exit {result.returncode}, expected {case['exit_code']}")
            matches = False
        if matches:
            print(f"PASS {case['name']}")
        else:
            print(result.stderr, end="")
            failed += 1
    result = subprocess.run([solver, "--help"], capture_output=True, text=True,
                            timeout=args.timeout, check=False)
    # Normalize CLI11's invocation path and trailing display whitespace only.
    help_text = result.stdout.replace(f"{solver} [OPTIONS]", "primo [OPTIONS]", 1)
    help_text = "\n".join(line.rstrip() for line in help_text.splitlines()).rstrip() + "\n"
    matches = check_text("CLI help", (EXAMPLES / "primo-help.txt").read_text(), help_text)
    if result.returncode != 0 or not matches:
        print(f"FAIL CLI help: exit {result.returncode}")
        failed += 1
    else:
        print("PASS CLI help")
    print(f"{len(cases) + 1 - failed}/{len(cases) + 1} documentation checks passed")
    return int(failed != 0)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        raise SystemExit(str(error)) from error
