#!/usr/bin/env python3
from __future__ import annotations

import argparse
import urllib.request
from pathlib import Path

DEFAULT_VERSION = "v2.6.2"
URL_TEMPLATE = "https://github.com/CLIUtils/CLI11/releases/download/{version}/CLI11.hpp"


def main() -> int:
    parser = argparse.ArgumentParser(description="Install the CLI11 single header.")
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument("--version", default=DEFAULT_VERSION)
    args = parser.parse_args()

    include_dir = args.prefix / "include" / "CLI"
    include_dir.mkdir(parents=True, exist_ok=True)
    destination = include_dir / "CLI.hpp"
    url = URL_TEMPLATE.format(version=args.version)

    print(f"Downloading CLI11 {args.version} from {url}")
    with urllib.request.urlopen(url, timeout=60) as response:
        destination.write_bytes(response.read())
    print(f"Installed {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
