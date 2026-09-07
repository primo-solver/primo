#!/usr/bin/env python3
"""Seed Python's RNG before executing the installed OpFuzz launcher.

The supported YinYang fork exposes --seed for TypeFuzz but not OpFuzz. This
adapter does not modify its mutation algorithm or dependency checkout.
"""

from __future__ import annotations

import argparse
import random
import runpy
import shutil
import sys


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument("--seed", type=int, required=True)
    args, remaining = parser.parse_known_args()
    random.seed(args.seed)
    executable = shutil.which("opfuzz")
    if executable is None:
        raise SystemExit("opfuzz is not installed on PATH")
    sys.argv = [executable, *remaining]
    runpy.run_path(executable, run_name="__main__")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
