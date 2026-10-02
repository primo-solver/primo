#!/usr/bin/env python3
"""Bundle installed documentation packages' license notices with their HTML assets."""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path

PACKAGES = (
    "sphinx", "jupyter-book", "sphinx-book-theme", "pydata-sphinx-theme",
    "sphinx-copybutton", "sphinx-design", "sphinx-togglebutton", "sphinx-thebe", "myst-nb",
)


def collect(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    packages = []
    for name in PACKAGES:
        package = importlib.metadata.distribution(name)
        notices = []
        for entry in package.files or []:
            if not any(word in entry.name.lower() for word in ("license", "copying")):
                continue
            source = package.locate_file(entry)
            if not source.is_file():
                continue
            data = source.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            filename = f"{name}-{digest[:12]}-{entry.name}"
            (output / filename).write_bytes(data)
            notices.append({"file": filename, "sha256": digest, "package_path": str(entry)})
        if not notices:
            raise ValueError(f"documentation dependency has no bundled license notice: {name}")
        packages.append({"name": name, "version": package.version, "notices": notices})
    (output / "index.json").write_text(json.dumps(packages, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    collect(parser.parse_args().output)


if __name__ == "__main__":
    main()
