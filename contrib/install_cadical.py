#!/usr/bin/env python3
"""Clone, build, and install CaDiCaL into contrib/local."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


REPO = "https://github.com/arminbiere/cadical.git"


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument("--tag", default="3.0.0")
    parser.add_argument("--repo", default=REPO)
    args = parser.parse_args()

    if shutil.which("git") is None:
        raise SystemExit("git is required to install CaDiCaL")
    if shutil.which("make") is None:
        raise SystemExit("make is required to install CaDiCaL")

    src_root = Path("contrib/src")
    src_root.mkdir(parents=True, exist_ok=True)
    source = src_root / "cadical"
    if source.exists():
        run(["git", "fetch", "--tags"], cwd=source)
    else:
        run(["git", "clone", args.repo, str(source)])

    run(["git", "checkout", args.tag], cwd=source)
    run(["./configure", f"--prefix={args.prefix.resolve()}"], cwd=source)
    run(["make", "-j"], cwd=source)
    run(["make", "install"], cwd=source)

    bin_path = args.prefix / "bin" / "cadical"
    if bin_path.exists():
        subprocess.run([str(bin_path), "--version"], check=False)
    else:
        print(f"CaDiCaL installed under {args.prefix}; no cadical binary found at {bin_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
