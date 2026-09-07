#!/usr/bin/env python3
"""Stage the build compiler's headers for header-poor SLURM compute nodes.

This is optional clang-tidy tooling, not a compiler or solver dependency. The
compiler's own search order is preserved, including its C system headers.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


def cxx_search_paths(output: str) -> list[Path]:
    active = False
    paths = []
    for line in output.splitlines():
        if line.strip() == "#include <...> search starts here:":
            active = True
        elif line.strip() == "End of search list.":
            active = False
        elif active:
            path = Path(line.strip())
            if path.is_absolute():
                paths.append(path)
    if not any("c++" in path.parts for path in paths):
        raise ValueError("compiler did not report C++ standard-library header paths")
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    probe = subprocess.run(
        [args.cxx, "-E", "-x", "c++", "-", "-v"],
        input="", text=True, capture_output=True, check=True,
    )
    paths = cxx_search_paths(probe.stderr)
    args.out.mkdir(parents=True, exist_ok=True)
    staged = []
    for index, source in enumerate(paths):
        destination = args.out.resolve() / str(index)
        shutil.copytree(source, destination, dirs_exist_ok=True)
        staged.append(str(destination))
    (args.out / "include-paths.txt").write_text("\n".join(staged) + "\n")
    (args.out / "compiler-search.txt").write_text(probe.stderr)
    print(f"staged {len(staged)} compiler include search roots under {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
