#!/usr/bin/env python3
"""Install ddSMT into contrib/local and expose a local wrapper."""

from __future__ import annotations

import argparse
import stat
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument("--package", default="ddsmt")
    args = parser.parse_args()

    python_dir = args.prefix / "python"
    args.prefix.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--upgrade",
            "--target",
            str(python_dir),
            args.package,
        ],
        check=True,
    )

    wrapper = args.prefix / "bin" / "ddsmt"
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    wrapper.write_text(
        f"""#!/usr/bin/env python3
import runpy
import sys

sys.path.insert(0, {str(python_dir.resolve())!r})
raise SystemExit(runpy.run_module("ddsmt.__main__", run_name="__main__"))
""",
    )
    wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    subprocess.run([str(wrapper), "--version"], check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
