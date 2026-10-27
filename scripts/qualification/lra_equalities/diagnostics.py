#!/usr/bin/env python3
"""Use the maintained bounded LRA profiler with the equality portfolio arms."""

import os
import sys
from pathlib import Path

from campaign import PROFILES


def main() -> int:
    sys.path.insert(0, os.environ.get(
        "PRIMO_LRA_ABLATION_WORKFLOW",
        str(Path(__file__).resolve().parents[1] / "lra_ablation")))
    import diagnose

    diagnose.PROFILES = PROFILES
    return diagnose.main()


if __name__ == "__main__":
    raise SystemExit(main())
