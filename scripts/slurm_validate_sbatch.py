#!/usr/bin/env python3
"""Validate generated SLURM batch scripts before submission.

The checker is intentionally conservative around embedded interpreter heredocs.
The recurring failure mode was a syntactically valid shell script whose
unquoted heredoc delimiter let the shell expand or strip characters from Python
source before Python saw it.  `bash -n` cannot catch that class of bug, so this
script adds a small static guard.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path

HEREDOC_RE = re.compile(
    r"<<-?\s*(?P<quoted>['\"]?)(?P<tag>[A-Za-z_][A-Za-z0-9_]*)['\"]?"
)
INTERPRETER_RE = re.compile(r"\b(python[0-9.]*|perl|ruby)\b")


def bash_syntax_check(path: Path) -> list[str]:
    proc = subprocess.run(
        ["bash", "-n", str(path)],
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode == 0:
        return []
    details = proc.stderr.strip() or proc.stdout.strip()
    return [f"{path}: bash -n failed: {details}"]


def heredoc_issues(path: Path, *, strict: bool) -> list[str]:
    issues: list[str] = []
    lines = path.read_text(errors="replace").splitlines()
    for line_number, line in enumerate(lines, start=1):
        match = HEREDOC_RE.search(line)
        if match is None:
            continue
        quoted = bool(match.group("quoted"))
        if quoted:
            continue
        if strict or INTERPRETER_RE.search(line):
            tag = match.group("tag")
            issues.append(
                f"{path}:{line_number}: unquoted heredoc delimiter {tag!r}; "
                "use <<'TAG' for embedded scripts"
            )
    return issues


def validate(path: Path, *, strict_heredocs: bool) -> list[str]:
    issues: list[str] = []
    if not path.is_file():
        return [f"{path}: not a file"]
    issues.extend(bash_syntax_check(path))
    issues.extend(heredoc_issues(path, strict=strict_heredocs))
    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("script", type=Path, nargs="+")
    parser.add_argument(
        "--strict-heredocs",
        action="store_true",
        help="reject every unquoted heredoc, not only interpreter heredocs",
    )
    args = parser.parse_args()

    all_issues: list[str] = []
    for script in args.script:
        all_issues.extend(validate(script, strict_heredocs=args.strict_heredocs))

    for issue in all_issues:
        print(issue)
    return 1 if all_issues else 0


if __name__ == "__main__":
    raise SystemExit(main())
