#!/usr/bin/env python3
"""Run an SMT-LIB file with a Python reference solver binding.

This wrapper lets the standard benchmark harness use Python packages such as
``z3-solver`` and ``cvc5`` when command-line solver binaries are unavailable on
SLURM nodes.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def run_z3(path: Path) -> str:
    import z3  # type: ignore[import-not-found]

    solver = z3.Solver()
    solver.from_file(str(path))
    return str(solver.check())


def run_cvc5(path: Path) -> str:
    import cvc5  # type: ignore[import-not-found]

    solver = cvc5.Solver()
    parser = cvc5.InputParser(solver)
    parser.setFileInput(cvc5.InputLanguage.SMT_LIB_2_6, str(path))
    result = "unknown"
    while True:
        command = parser.nextCommand()
        if command.isNull():
            break
        value = command.invoke(solver, parser.getSymbolManager())
        if command.getCommandName() == "check-sat":
            result = str(value)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", choices=["z3", "cvc5"], required=True)
    parser.add_argument("file", type=Path)
    args = parser.parse_args()

    try:
        if args.solver == "z3":
            result = run_z3(args.file)
        else:
            result = run_cvc5(args.file)
    except Exception as exc:  # noqa: BLE001 - benchmark harness records stderr.
        print(f"{type(exc).__name__}: {exc}", file=sys.stderr)
        return 1

    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
