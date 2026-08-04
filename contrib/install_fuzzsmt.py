#!/usr/bin/env python3
"""Install FuzzSMT into contrib/local."""

from __future__ import annotations

import argparse
import shutil
import stat
import subprocess
from pathlib import Path

REPO = "https://github.com/msoos/fuzzsmt.git"


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument("--repo", default=REPO)
    args = parser.parse_args()

    if shutil.which("git") is None:
        raise SystemExit("git is required to install FuzzSMT")
    if shutil.which("java") is None:
        raise SystemExit("java is required to run FuzzSMT")

    src_root = Path("contrib/src")
    src_root.mkdir(parents=True, exist_ok=True)
    source = src_root / "fuzzsmt"
    if source.exists():
        run(["git", "fetch", "--tags"], cwd=source)
        run(["git", "checkout", "master"], cwd=source)
        run(["git", "pull", "--ff-only"], cwd=source)
    else:
        run(["git", "clone", args.repo, str(source)])

    share_dir = args.prefix / "share" / "fuzzsmt"
    bin_dir = args.prefix / "bin"
    share_dir.mkdir(parents=True, exist_ok=True)
    bin_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "fuzzsmt.jar", share_dir / "fuzzsmt.jar")
    shutil.copy2(source / "COPYING", share_dir / "COPYING")

    wrapper = bin_dir / "fuzzsmt"
    wrapper.write_text(
        f"""#!/usr/bin/env sh
exec java -jar {str((share_dir / "fuzzsmt.jar").resolve())!r} "$@"
""",
    )
    wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    subprocess.run([str(wrapper), "-V"], check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
