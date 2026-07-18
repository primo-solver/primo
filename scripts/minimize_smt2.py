#!/usr/bin/env python3
"""Minimize failing SMT-LIB inputs while preserving a failure predicate."""

from __future__ import annotations

import argparse
import shutil
import shlex
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument(
        "--predicate", required=True, help="Command that reproduces the failure"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ddsmt", default="ddsmt")
    parser.add_argument(
        "--match-out", help="Require this text on stdout to preserve the failure"
    )
    parser.add_argument(
        "--match-err", help="Require this text on stderr to preserve the failure"
    )
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    ddsmt = shutil.which(args.ddsmt)
    if ddsmt is None:
        shutil.copyfile(args.input, args.output)
        print(f"ddSMT is not installed; copied input to {args.output}")
        return 0

    command = [ddsmt, "--timeout", str(args.timeout)]
    if args.match_out:
        command.extend(["--match-out", args.match_out])
    if args.match_err:
        command.extend(["--match-err", args.match_err])
    command.extend(
        [
            str(args.input),
            str(args.output),
            *shlex.split(args.predicate),
        ],
    )
    subprocess.run(command, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
