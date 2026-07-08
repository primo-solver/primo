#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import urllib.request
import zipfile
from pathlib import Path


VERSION = "4.13.2"
ANTLR_JAR_URL = f"https://www.antlr.org/download/antlr-{VERSION}-complete.jar"
ANTLR_SOURCE_URL = f"https://github.com/antlr/antlr4/archive/refs/tags/{VERSION}.zip"


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        return
    print(f"Downloading {url}")
    with urllib.request.urlopen(url, timeout=120) as response:
        destination.write_bytes(response.read())


def main() -> int:
    parser = argparse.ArgumentParser(description="Install ANTLR4 jar and C++ runtime.")
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument("--src-root", type=Path, default=Path("contrib/src"))
    parser.add_argument("--downloads", type=Path, default=Path("contrib/downloads"))
    args = parser.parse_args()

    jar = args.prefix / "share" / "java" / f"antlr-{VERSION}-complete.jar"
    download(ANTLR_JAR_URL, jar)

    archive = args.downloads / f"antlr4-{VERSION}.zip"
    download(ANTLR_SOURCE_URL, archive)

    source_root = args.src_root / f"antlr4-{VERSION}"
    if not source_root.exists():
        args.src_root.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive) as zip_file:
            zip_file.extractall(args.src_root)
        extracted = args.src_root / f"antlr4-{VERSION}"
        if extracted != source_root and extracted.exists():
            shutil.move(extracted, source_root)

    build_dir = source_root / "runtime" / "Cpp" / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "cmake",
            "-S",
            str(source_root / "runtime" / "Cpp"),
            "-B",
            str(build_dir),
            f"-DCMAKE_INSTALL_PREFIX={args.prefix.resolve()}",
            "-DANTLR_BUILD_CPP_TESTS=OFF",
            "-DANTLR_BUILD_SHARED=OFF",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        ],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(build_dir)], check=True)
    subprocess.run(["cmake", "--install", str(build_dir)], check=True)
    print(f"Installed ANTLR4 {VERSION} under {args.prefix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
