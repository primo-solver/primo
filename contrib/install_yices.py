#!/usr/bin/env python3
"""Install a Yices 2 binary from GitHub releases into contrib/local.

Yices is a *reference solver only*, in exactly the sense z3 and cvc5 are: we
run it as a differential oracle and never link against it or read its source.
That boundary matters more for Yices than for the others, because Yices is the
solver Dutertre and de Moura wrote alongside "Integrating Simplex with
DPLL(T)" -- the paper Primo's QF_LRA theory follows. Reading the paper is the
whole point; reading the implementation would be a clean-room violation. The
binary installs under contrib/local, which is gitignored and excluded from the
cluster rsync, so nothing here reaches the repository.

Yices 2 is GPLv3. Running it as a separate process imposes no obligation on
Primo; linking against libyices would, so the `lib/` and `include/` directories
in the release archive are deliberately not installed.

The SMT-LIB 2 front end is `yices-smt2`, not `yices` (which speaks the native
Yices language). Only the former is installed.
"""

from __future__ import annotations

import argparse
import json
import platform
import shutil
import stat
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path

API_URL = "https://api.github.com/repos/SRI-CSL/yices2/releases/latest"


def system_asset_keywords() -> list[str]:
    machine = platform.machine().lower()
    system = platform.system().lower()
    arch_terms = ["x86_64", "amd64"] if machine in {"x86_64", "amd64"} else [machine]
    if system == "linux":
        return ["linux", *arch_terms]
    if system == "darwin":
        return ["darwin", "apple", *arch_terms]
    raise SystemExit(
        f"unsupported platform for automatic yices install: {platform.system()} {platform.machine()}"
    )


def choose_asset(release: dict) -> dict:
    keywords = system_asset_keywords()
    candidates = []
    for asset in release.get("assets", []):
        name = asset.get("name", "").lower()
        if not name.endswith((".tar.gz", ".tgz")):
            continue
        score = sum(1 for keyword in keywords if keyword in name)
        # Same reasoning as install_cvc5.py: only the binary is copied out of
        # the archive, so a build that needs a companion libgmp.so at runtime
        # would install as something that cannot start. The upstream assets
        # are named "-static-gmp".
        if score and "static" in name:
            score += 1
        if score:
            candidates.append((score, asset))
    if not candidates:
        names = ", ".join(asset.get("name", "") for asset in release.get("assets", []))
        raise SystemExit(
            f"no matching yices release asset for this platform; assets: {names}"
        )
    candidates.sort(key=lambda item: item[0], reverse=True)
    return candidates[0][1]


def download(url: str, destination: Path) -> None:
    print(f"downloading {url}")
    with urllib.request.urlopen(url) as response, destination.open("wb") as out:
        shutil.copyfileobj(response, out)


def find_binary(root: Path) -> Path:
    for path in root.rglob("yices-smt2"):
        if path.is_file():
            return path
    raise SystemExit("downloaded yices archive did not contain a yices-smt2 binary")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument(
        "--version", help="Release tag, for example yices-2.7.0. Defaults to latest."
    )
    args = parser.parse_args()

    if args.version:
        api_url = (
            f"https://api.github.com/repos/SRI-CSL/yices2/releases/tags/{args.version}"
        )
    else:
        api_url = API_URL
    with urllib.request.urlopen(api_url) as response:
        release = json.load(response)

    asset = choose_asset(release)
    args.prefix.mkdir(parents=True, exist_ok=True)
    downloads = Path("contrib/downloads")
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / asset["name"]
    if not archive.exists():
        download(asset["browser_download_url"], archive)

    with tempfile.TemporaryDirectory() as tmp:
        with tarfile.open(archive) as tf:
            tf.extractall(Path(tmp))
        binary = find_binary(Path(tmp))
        target = args.prefix / "bin" / "yices-smt2"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(binary, target)
        target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    subprocess.run([str(args.prefix / "bin" / "yices-smt2"), "--version"], check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
