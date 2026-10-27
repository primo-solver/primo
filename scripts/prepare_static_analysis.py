#!/usr/bin/env python3
"""Validate analysis coverage and describe flags for tracked non-build examples.

Reads a configured compilation database; executes no compiler or solver.
Production sources and unit tests require actual build-system entries. The
standalone rational benchmark and documentation examples explicitly reuse
their owning layer's flags, with each derived entry recorded in the manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shlex
import subprocess
from collections import defaultdict
from pathlib import Path


def tracked_sources(repo: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "-C", str(repo), "ls-files", "-z"],
        capture_output=True,
        check=True,
    )
    return sorted(
        (repo / name.decode()).resolve()
        for name in result.stdout.split(b"\0")
        if name.endswith((b".cpp", b".hpp", b".h"))
        and name.startswith((b"src/", b"tests/", b"docs/examples/"))
    )


def source_argument(arguments: list[str], directory: Path, source: Path) -> int:
    matches = [
        index
        for index, arg in enumerate(arguments)
        if not arg.startswith("-") and (directory / arg).resolve() == source
    ]
    if len(matches) != 1:
        raise ValueError(f"cannot identify source argument: {source}")
    return matches[0]


def prepare(repo: Path, build: Path, output: Path) -> dict:
    repo = repo.resolve()
    database = (build / "compile_commands.json").resolve()
    if output.resolve() == database.parent:
        raise ValueError("analysis output must be separate from the build database")
    if output.exists():
        raise ValueError("analysis output already exists; use a fresh attempt")
    sources = tracked_sources(repo)
    expected = {path for path in sources if path.suffix == ".cpp"}
    revision_header = database.parent / "generated/primo_git_revision.hpp"
    if repo / "src/main.cpp" in expected and not revision_header.is_file():
        raise ValueError("missing generated revision header; run make analysis-db")
    entries = []
    by_file = defaultdict(list)
    for row in json.loads(database.read_text()):
        directory = Path(row["directory"])
        if not directory.is_absolute():
            directory = database.parent / directory
        directory = directory.resolve()
        source = (directory / row["file"]).resolve()
        if source not in expected:
            continue
        arguments = row.get("arguments") or shlex.split(row["command"])
        if not arguments:
            raise ValueError(f"empty compiler command: {source}")
        source_argument(arguments, directory, source)
        entry = {
            "directory": str(directory),
            "file": str(source),
            "arguments": arguments,
        }
        entries.append(entry)
        by_file[source].append(entry)

    derived = []
    for source in sorted(expected - set(by_file)):
        relative = source.relative_to(repo)
        if relative == Path("tests/bench/word_gcd_bench.cpp"):
            template = repo / "src/util/rational.cpp"
        elif relative.is_relative_to("docs/examples"):
            template = repo / "src/smt/solver_engines.cpp"
        else:
            raise ValueError(
                f"missing production/unit-test compile command: {relative}"
            )
        if template not in by_file:
            raise ValueError(
                f"missing analysis flag source: {template.relative_to(repo)}"
            )
        for row in by_file[template]:
            arguments = list(row["arguments"])
            index = source_argument(arguments, Path(row["directory"]), template)
            arguments[index] = str(source)
            entries.append({**row, "file": str(source), "arguments": arguments})
            derived.append(
                {"file": str(relative), "flags_from": str(template.relative_to(repo))}
            )

    if {Path(entry["file"]) for entry in entries} != expected:
        raise ValueError(
            "analysis database does not cover the tracked translation units"
        )
    manifest = {
        "source_root": str(repo),
        "translation_units": len(expected),
        "compile_commands": len(entries),
        "derived_commands": derived,
        "input_database_sha256": hashlib.sha256(database.read_bytes()).hexdigest(),
        "source_sha256": {
            str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sources
        },
        "generated_revision_header": (
            {
                "path": str(revision_header),
                "sha256": hashlib.sha256(revision_header.read_bytes()).hexdigest(),
            }
            if revision_header.is_file()
            else None
        ),
        "scope": "Source analysis only; derived commands are not evidence of a built executable.",
    }
    output.mkdir(parents=True)
    (output / "compile_commands.json").write_text(json.dumps(entries, indent=2) + "\n")
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    manifest = prepare(args.repo, args.build, args.out)
    print(
        json.dumps(
            {
                key: manifest[key]
                for key in ("translation_units", "compile_commands", "derived_commands")
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
