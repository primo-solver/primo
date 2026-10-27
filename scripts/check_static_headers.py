#!/usr/bin/env python3
"""Parse each owned header by itself with the configured SMT compile flags."""

from __future__ import annotations

import argparse
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def syntax_command(entry: dict, source: Path) -> list[str]:
    arguments = entry["arguments"]
    result = []
    skip = False
    for argument in arguments:
        if skip:
            skip = False
        elif argument == "-o":
            skip = True
        elif argument != "-c" and (
            Path(entry["directory"]) / argument
        ).resolve() != Path(entry["file"]):
            result.append(argument)
    return [*result, "-fsyntax-only", "-x", "c++", str(source)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    if args.jobs < 1 or args.timeout < 1:
        parser.error("jobs and timeout must be positive")
    if args.out.exists():
        parser.error("output directory already exists; use a fresh attempt")
    repo = args.repo.resolve()
    entries = json.loads(args.database.read_text())
    contexts = [
        row
        for row in entries
        if Path(row["file"]) == repo / "src/smt/solver_engines.cpp"
    ]
    if not contexts:
        parser.error("database has no SMT library compile context")
    tracked = subprocess.run(
        ["git", "-C", str(repo), "ls-files", "-z", "--", "src"],
        check=True,
        capture_output=True,
    ).stdout.split(b"\0")
    headers = sorted(
        name.decode() for name in tracked if name.endswith((b".hpp", b".h"))
    )
    args.out.mkdir(parents=True)

    def check(task):
        index, name, context = task
        stem = args.out / f"{index:04d}"
        source = stem.with_suffix(".cpp").resolve()
        source.write_text("#include " + json.dumps(str(repo / name)) + "\n")
        command = syntax_command(context, source)
        # GNU timeout controls the compiler process group, including cc1 children.
        process = subprocess.run(
            ["timeout", "-k", "2s", f"{args.timeout}s", *command],
            cwd=context["directory"],
            capture_output=True,
            text=True,
            check=False,
        )
        stem.with_suffix(".stdout").write_text(process.stdout)
        stem.with_suffix(".stderr").write_text(process.stderr)
        return {"file": name, "exit": process.returncode, "command": command}

    tasks = [
        (index, name, context)
        for index, (name, context) in enumerate(
            (name, context) for name in headers for context in contexts
        )
    ]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(check, tasks))
    report = {
        "headers": len(headers),
        "contexts": len(contexts),
        "checks": len(rows),
        "passed": sum(row["exit"] == 0 for row in rows),
        "rows": rows,
        "timeout_seconds": args.timeout,
        "jobs": args.jobs,
        "scope": "Standalone header syntax; no code generation, linking, or solver execution.",
    }
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Header checks: {report['passed']}/{len(rows)} passed")
    return int(any(row["exit"] != 0 for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
