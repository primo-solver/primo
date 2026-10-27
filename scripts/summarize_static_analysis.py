#!/usr/bin/env python3
"""Check source coverage, input stability, tool success and distinct diagnostics."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

COMMAND = re.compile(r"^(?:\S*/)?clang-tidy(?:-\d+)?\s", re.MULTILINE)
DIAGNOSTIC = re.compile(
    r"^(.+?):(\d+):(\d+): (warning|error|fatal error): (.*?)(?: \[([^\]]+)\])?$",
    re.MULTILINE,
)


def diagnostics(text: str, repo: Path) -> list[dict]:
    rows = []
    for match in DIAGNOSTIC.finditer(text):
        path, line, column, severity, message, check = match.groups()
        file = Path(path)
        if file.is_relative_to(repo):
            path = str(file.relative_to(repo))
        rows.append(
            {
                "file": path,
                "line": int(line),
                "column": int(column),
                "severity": severity,
                "message": message,
                "check": check or "compiler-diagnostic",
            }
        )
    return rows


def summarize(manifest: dict, text: str, exit_code: int) -> dict:
    repo = Path(manifest["source_root"])
    expected = {name for name in manifest["source_sha256"] if name.endswith(".cpp")}
    changed = [
        name
        for name, digest in manifest["source_sha256"].items()
        if not (repo / name).is_file()
        or hashlib.sha256((repo / name).read_bytes()).hexdigest() != digest
    ]
    generated = manifest.get("generated_revision_header")
    if generated:
        path = Path(generated["path"])
        if (
            not path.is_file()
            or hashlib.sha256(path.read_bytes()).hexdigest() != generated["sha256"]
        ):
            changed.append("generated/primo_git_revision.hpp")
    seen = set()
    for match in COMMAND.finditer(text):
        command = text[match.start() :].splitlines()[0]
        sources = [
            name for name in expected if command.endswith(" " + str(repo / name))
        ]
        if len(sources) != 1:
            raise ValueError(f"unrecognized analysis command: {command}")
        seen.add(sources[0])
    rows = diagnostics(text, repo)
    unique = {tuple(row.items()): row for row in rows}
    errors = [row for row in unique.values() if row["severity"] != "warning"]
    missing = sorted(expected - seen)
    # The LLVM runner reports only a global exit status, not each worker's status.
    # Never override a failed aggregate using a selected successful retry.
    incomplete = bool(missing or errors or changed or exit_code != 0)
    status = (
        "incomplete" if incomplete else "complete-with-findings" if rows else "clean"
    )
    return {
        "status": status,
        "expected_translation_units": len(expected),
        "analyzed_translation_units": len(seen),
        "missing": missing,
        "changed_inputs": changed,
        "tool_exit_code": exit_code,
        "diagnostic_occurrences": len(rows),
        "unique_diagnostics": len(unique),
        "counts": dict(Counter(row["check"] for row in unique.values())),
        "diagnostics": sorted(
            unique.values(),
            key=lambda row: (row["file"], row["line"], row["column"], row["check"]),
        ),
        "scope": "Diagnostic inventory; completed analysis is not a claim that findings are benign.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--exit-code", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = summarize(
        json.loads(args.manifest.read_text()), args.log.read_text(), args.exit_code
    )
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {
                key: report[key]
                for key in (
                    "status",
                    "analyzed_translation_units",
                    "unique_diagnostics",
                    "counts",
                )
            },
            indent=2,
        )
    )
    return int(report["status"] == "incomplete")


if __name__ == "__main__":
    raise SystemExit(main())
