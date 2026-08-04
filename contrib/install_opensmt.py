#!/usr/bin/env python3
"""Install an OpenSMT2 binary from GitHub releases into contrib/local.

OpenSMT is a *reference solver only*, on the same footing as z3, cvc5 and
Yices: we run it as a differential oracle and never link against it or read
its source. The caution that applies to Yices applies here too -- OpenSMT's
QF_LRA theory solver also descends from Dutertre and de Moura's "Integrating
Simplex with DPLL(T)", the paper Primo's own solver follows. Reading the paper
is the point; reading the implementation would be a clean-room violation.

OpenSMT is MIT-licensed, so unlike Yices there is no linking obligation to
avoid -- but the archive is still unpacked for the `opensmt` binary alone,
because nothing else in it is wanted and a smaller install is a smaller
temptation.

Assets are `.tar.bz2` rather than the `.tar.gz`/`.zip` the other installers
see; `tarfile.open` picks the codec from the stream, so no special handling is
needed. The static Linux asset is preferred for the same reason as in
install_cvc5.py: only the binary is copied out, so a build that needs a
companion shared library would install as something that cannot start.

The binary installs under contrib/local, which is gitignored and excluded from
the cluster rsync, so nothing here reaches the repository.
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

API_URL = (
    "https://api.github.com/repos/usi-verification-and-security/opensmt/releases/latest"
)


def system_asset_keywords() -> list[str]:
    machine = platform.machine().lower()
    system = platform.system().lower()
    if system == "linux":
        # Upstream names the Linux assets "x64", not "x86_64" or "amd64".
        arch_terms = ["x64"] if machine in {"x86_64", "amd64"} else [machine]
        return ["linux", *arch_terms]
    if system == "darwin":
        arch_terms = ["arm"] if machine in {"arm64", "aarch64"} else ["x64"]
        return ["osx", *arch_terms]
    raise SystemExit(
        f"unsupported platform for automatic opensmt install: "
        f"{platform.system()} {platform.machine()}"
    )


def choose_asset(release: dict) -> dict:
    keywords = system_asset_keywords()
    candidates = []
    for asset in release.get("assets", []):
        name = asset.get("name", "").lower()
        if not name.endswith((".tar.bz2", ".tar.gz", ".tgz")):
            continue
        # Every keyword must match: "opensmt-x64-linux" and "opensmt-arm-osx"
        # share enough substrings that a partial match picks the wrong OS.
        if not all(keyword in name for keyword in keywords):
            continue
        score = 2 if "static" in name else 1
        candidates.append((score, asset))
    if not candidates:
        names = ", ".join(asset.get("name", "") for asset in release.get("assets", []))
        raise SystemExit(
            f"no matching opensmt release asset for this platform; assets: {names}"
        )
    candidates.sort(key=lambda item: item[0], reverse=True)
    return candidates[0][1]


def download(url: str, destination: Path) -> None:
    print(f"downloading {url}")
    with urllib.request.urlopen(url) as response, destination.open("wb") as out:
        shutil.copyfileobj(response, out)


def find_binary(root: Path) -> Path:
    for path in root.rglob("opensmt"):
        if path.is_file():
            return path
    raise SystemExit("downloaded opensmt archive did not contain an opensmt binary")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument(
        "--version", help="Release tag, for example v2.9.2. Defaults to latest."
    )
    args = parser.parse_args()

    if args.version:
        api_url = (
            "https://api.github.com/repos/usi-verification-and-security/"
            f"opensmt/releases/tags/{args.version}"
        )
    else:
        api_url = API_URL
    with urllib.request.urlopen(api_url) as response:
        release = json.load(response)

    asset = choose_asset(release)
    print(f"selected {asset['name']} from {release.get('tag_name')}")
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
        target = args.prefix / "bin" / "opensmt"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(binary, target)
        target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    subprocess.run([str(args.prefix / "bin" / "opensmt"), "--version"], check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
