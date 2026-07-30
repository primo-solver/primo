#!/usr/bin/env python3
"""Install a cvc5 binary from GitHub releases into contrib/local."""

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
import zipfile
from pathlib import Path


API_URL = "https://api.github.com/repos/cvc5/cvc5/releases/latest"


def system_asset_keywords() -> list[str]:
    machine = platform.machine().lower()
    system = platform.system().lower()
    arch_terms = ["x86_64", "amd64"] if machine in {"x86_64", "amd64"} else [machine]
    if system == "linux":
        return ["linux", *arch_terms]
    if system == "darwin":
        return ["macos", "mac", "darwin", *arch_terms]
    raise SystemExit(
        f"unsupported platform for automatic cvc5 install: {platform.system()} {platform.machine()}"
    )


def choose_asset(release: dict) -> dict:
    keywords = system_asset_keywords()
    candidates = []
    for asset in release.get("assets", []):
        name = asset.get("name", "").lower()
        if not name.endswith((".zip", ".tar.gz", ".tgz")):
            continue
        if "cvc5" not in name:
            continue
        score = sum(1 for keyword in keywords if keyword in name)
        # Prefer statically-linked assets: this script only copies the
        # `cvc5` binary itself (see the unpack loop below), never the
        # accompanying lib/*.so files a "shared"/"libcxx-shared" build
        # needs at runtime -- installing one of those without also
        # installing its shared libraries produces a binary that fails to
        # even start ("error while loading shared libraries"), found the
        # hard way (missing libc++abi.so.1 et al.) while trying to use a
        # previously-installed libcxx-shared cvc5 for a benchmark
        # comparison. A "static" asset has no such runtime dependency.
        if score and "static" in name and "gpl" not in name:
            score += 1
        if score:
            candidates.append((score, asset))
    if not candidates:
        names = ", ".join(asset.get("name", "") for asset in release.get("assets", []))
        raise SystemExit(
            f"no matching cvc5 release asset for this platform; assets: {names}"
        )
    candidates.sort(key=lambda item: item[0], reverse=True)
    return candidates[0][1]


def download(url: str, destination: Path) -> None:
    print(f"downloading {url}")
    with urllib.request.urlopen(url) as response, destination.open("wb") as out:
        shutil.copyfileobj(response, out)


def unpack(archive: Path, destination: Path) -> None:
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(destination)
    else:
        with tarfile.open(archive) as tf:
            tf.extractall(destination)


def find_binary(root: Path) -> Path:
    for path in root.rglob("cvc5"):
        if path.is_file():
            return path
    raise SystemExit("downloaded cvc5 archive did not contain a cvc5 binary")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument(
        "--version", help="Release tag, for example cvc5-1.3.0. Defaults to latest."
    )
    args = parser.parse_args()

    if args.version:
        api_url = f"https://api.github.com/repos/cvc5/cvc5/releases/tags/{args.version}"
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
        unpack(archive, Path(tmp))
        binary = find_binary(Path(tmp))
        target = args.prefix / "bin" / "cvc5"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(binary, target)
        target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    subprocess.run([str(args.prefix / "bin" / "cvc5"), "--version"], check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
