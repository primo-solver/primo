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
    parser.add_argument("--tag", default="rel-3.0.0")
    parser.add_argument("--repo", default=REPO)
    parser.add_argument(
        "--clean-build",
        action="store_true",
        help="Remove CaDiCaL's in-tree build artifacts before configuring the selected tag.",
    )
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
    if args.clean_build:
        for path in [
            source / "build",
            source / "makefile",
            source / "src" / "makefile",
        ]:
            if path.is_dir():
                shutil.rmtree(path)
            elif path.exists() or path.is_symlink():
                path.unlink()
    run(["./configure"], cwd=source)
    run(["make", "-j"], cwd=source)

    include_dir = args.prefix / "include"
    lib_dir = args.prefix / "lib"
    bin_dir = args.prefix / "bin"
    include_dir.mkdir(parents=True, exist_ok=True)
    lib_dir.mkdir(parents=True, exist_ok=True)
    bin_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(source / "src" / "ipasir.h", include_dir / "ipasir.h")
    shutil.copy2(source / "src" / "cadical.hpp", include_dir / "cadical.hpp")

    library_candidates = list(source.glob("build/libcadical.a")) + list(
        source.glob("build/*/libcadical.a")
    )
    if not library_candidates:
        raise SystemExit("CaDiCaL build did not produce libcadical.a")
    shutil.copy2(library_candidates[0], lib_dir / "libcadical.a")

    binary_candidates = list(source.glob("build/cadical")) + list(
        source.glob("build/*/cadical")
    )
    if binary_candidates:
        shutil.copy2(binary_candidates[0], bin_dir / "cadical")
        (bin_dir / "cadical").chmod(0o755)

    bin_path = bin_dir / "cadical"
    if bin_path.exists():
        subprocess.run([str(bin_path), "--version"], check=False)
    else:
        print(
            f"CaDiCaL installed under {args.prefix}; no cadical binary found at {bin_path}"
        )
    metadata = args.prefix / "cadical-install.txt"
    metadata.write_text(f"repo={args.repo}\ntag={args.tag}\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
