#!/usr/bin/env python3
"""Download, build, and install GMP (with the C++ wrapper) into contrib/local."""

from __future__ import annotations

import argparse
import lzma
import os
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path

DEFAULT_VERSION = "6.3.0"
URL_TEMPLATE = "https://ftp.gnu.org/gnu/gmp/gmp-{version}.tar.xz"


def run(command: list[str], cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument("--version", default=DEFAULT_VERSION)
    parser.add_argument("--url", default=None)
    parser.add_argument(
        "--jobs", type=int, default=4, help="Parallel make jobs for the GMP build."
    )
    args = parser.parse_args()

    if shutil.which("make") is None:
        raise SystemExit("make is required to install GMP")

    prefix = args.prefix.resolve()
    src_root = Path("contrib/src")
    src_root.mkdir(parents=True, exist_ok=True)
    extract_dir = src_root / f"gmp-{args.version}"
    source_dir = src_root / "gmp"

    if not extract_dir.exists():
        url = args.url or URL_TEMPLATE.format(version=args.version)
        archive = src_root / f"gmp-{args.version}.tar.xz"
        print(f"Downloading GMP {args.version} from {url}")
        with urllib.request.urlopen(url, timeout=120) as response:
            archive.write_bytes(response.read())
        print(f"Extracting {archive}")
        with lzma.open(archive) as xz, tarfile.open(fileobj=xz) as tar:
            tar.extractall(src_root)
        archive.unlink()

    # Build out of a stable "gmp" symlink-free copy so re-runs with a
    # different --version don't reuse a stale configure/build tree.
    if source_dir.exists():
        shutil.rmtree(source_dir)
    shutil.copytree(extract_dir, source_dir)

    # GMP 6.3.0's configure probes rely on old-style K&R function
    # declarations (e.g. calling `g()` with arguments) that are legal
    # under C17's "unspecified parameters" semantics but a hard error
    # under C23, where an empty parameter list means "takes no
    # arguments". GCC 15+ defaults to -std=gnu23, which makes every
    # candidate compiler fail configure's compiler checks. Force an
    # older standard so configure can find a working compiler.
    configure_env = os.environ.copy()
    configure_env["CFLAGS"] = f"{configure_env.get('CFLAGS', '')} -std=gnu17".strip()

    run(
        [
            "./configure",
            f"--prefix={prefix}",
            "--enable-cxx",
            "--disable-shared",
            "--with-pic",
        ],
        cwd=source_dir,
        env=configure_env,
    )
    run(["make", f"-j{args.jobs}"], cwd=source_dir)
    run(["make", "check"], cwd=source_dir)
    run(["make", "install"], cwd=source_dir)

    metadata = args.prefix / "gmp-install.txt"
    metadata.write_text(f"version={args.version}\n", encoding="utf-8")
    print(f"GMP {args.version} installed under {prefix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
