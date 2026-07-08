#!/usr/bin/env python3
"""Install Z3 into contrib/local and expose a local wrapper."""

from __future__ import annotations

import argparse
import stat
import subprocess
import sys
from pathlib import Path


def write_wrapper(path: Path, python_dir: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"""#!/usr/bin/env python3
import runpy
import sys

sys.path.insert(0, {str(python_dir.resolve())!r})
raise SystemExit(runpy.run_module("z3.z3", run_name="__main__"))
""",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument("--package", default="z3-solver")
    args = parser.parse_args()

    args.prefix.mkdir(parents=True, exist_ok=True)
    python_dir = args.prefix / "python"
    subprocess.run(
        [sys.executable, "-m", "pip", "install", "--upgrade", "--target", str(python_dir), args.package],
        check=True,
    )

    wrapper = args.prefix / "bin" / "z3"
    packaged_binary = python_dir / "bin" / "z3"
    if packaged_binary.exists():
        wrapper.parent.mkdir(parents=True, exist_ok=True)
        wrapper.write_bytes(packaged_binary.read_bytes())
        wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        print(f"installed z3 binary wrapper: {wrapper}")
    else:
        write_wrapper(wrapper, python_dir)
        print(f"created z3 Python import wrapper: {wrapper}")

    subprocess.run([str(wrapper), "--version"], check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
