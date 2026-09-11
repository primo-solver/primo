#!/usr/bin/env python3
"""Full production-parser traversal and bounded execution of untouched native sessions."""

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import time
from pathlib import Path

from validate_incremental import run_command, validate, write_json


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def declared_answers(path, query_ends):
    """Archive-specific oracle: require a status immediately before each parsed query."""
    answers = []
    with path.open("rb") as stream:
        previous = 0
        for end in query_ends:
            if end <= previous:
                raise ValueError("non-increasing parser query offsets")
            start = max(0, end - 256)
            stream.seek(start)
            raw = stream.read(end - start)
            match = re.search(
                rb"\(set-info\s+:status\s+(sat|unsat|unknown)\)\s*\(check-sat\)$", raw
            )
            if not match:
                raise ValueError(f"no adjacent status at query offset {end}")
            answers.append(match[1].decode())
            previous = end
    return answers


def classify_execution(code, timed_out, stdout, expected):
    lines = stdout.splitlines()
    answers = [line for line in lines if line in {"sat", "unsat", "unknown"}]
    mismatch = [
        i
        for i, answer in enumerate(answers)
        if answer != "unknown"
        and (i >= len(expected) or (expected[i] != "unknown" and answer != expected[i]))
    ]
    if timed_out:
        outcome = "timeout"
    elif code != 0:
        outcome = "process-error"
    elif any(line.startswith("(error") for line in lines):
        outcome = "solver-error"
    elif mismatch:
        outcome = "wrong-answer"
    elif "unknown" in answers:
        outcome = "unknown"
    elif len(answers) == len(expected) and len(lines) == len(answers):
        outcome = (
            "complete-with-unverified-answers" if "unknown" in expected else "pass"
        )
    else:
        outcome = "incomplete-or-unsupported"
    return {
        "outcome": outcome,
        "exit_code": code,
        "timed_out": timed_out,
        "answers": answers,
        "expected_queries": len(expected),
        "queries_without_status_oracle": expected.count("unknown"),
        "mismatches": mismatch,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--index", type=int, required=True)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--parser-probe", required=True)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--dolmen", default="dolmen")
    parser.add_argument("--parse-timeout", type=float, default=120)
    parser.add_argument("--session-timeout", type=float, default=120)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--dolmen-timeout", type=float, default=30)
    parser.add_argument("--prefix-queries", type=int, default=4)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if (
        min(
            args.parse_timeout,
            args.session_timeout,
            args.timeout,
            args.dolmen_timeout,
            args.prefix_queries,
        )
        <= 0
    ):
        parser.error("budgets must be positive")
    args.solver_arg = ["--quiet", "--max-theory-conflicts=10000"]
    args.ref, args.ref_arg, args.chunk_bytes = "unused-declared-status-oracle", [], 4093
    entries = json.loads(args.manifest.read_text())["entries"]
    if not 0 <= args.index < len(entries):
        parser.error("index out of bounds")
    entry = entries[args.index]
    path = args.root / entry["file"]
    version = subprocess.run(
        [args.solver, "--version"], capture_output=True, text=True, check=True
    ).stdout
    if f"git: {args.expected_git}" not in version.splitlines():
        parser.error("solver revision mismatch")
    args.out.mkdir(parents=True, exist_ok=False)
    if digest(path) != entry["sha256"]:
        raise ValueError("source hash mismatch")
    binaries = {
        name: digest(Path(shutil.which(command) or command))
        for name, command in {
            "solver": args.solver,
            "parser": args.parser_probe,
            "dolmen": args.dolmen,
        }.items()
    }
    write_json(
        args.out / "run.json",
        {
            "entry": entry,
            "solver_version": version,
            "binary_sha256": binaries,
            "manifest_sha256": digest(args.manifest),
            "dolmen_version": subprocess.run(
                [args.dolmen, "--version"], capture_output=True, text=True, check=True
            ).stdout,
            "arguments": {
                k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()
            },
        },
    )
    status, code, stdout, stderr, seconds = run_command(
        [args.parser_probe, str(path)], args.parse_timeout
    )
    (args.out / "parser.stdout").write_text(stdout)
    (args.out / "parser.stderr").write_text(stderr)
    result = {
        "file": entry["file"],
        "parser": {"status": status, "exit_code": code, "seconds": seconds},
    }
    write_json(args.out / "result.json", result)
    if status != "done" or code != 0:
        return 1
    traversal = json.loads(stdout)
    expected = declared_answers(path, traversal["query_ends"])
    if len(expected) != entry["queries"]:
        raise ValueError("query count differs from manifest")
    result["parser"]["queries"] = len(expected)
    result["parser"]["outcome"] = "pass"
    write_json(args.out / "oracle.json", expected)
    # Execute the original bytes with real stdin; never flatten or rewrite scopes.
    start = time.monotonic()
    timed_out = False
    with (
        path.open("rb") as source,
        (args.out / "session.stdout").open("wb") as out,
        (args.out / "session.stderr").open("wb") as err,
    ):
        process = subprocess.Popen(
            [args.solver, *args.solver_arg], stdin=source, stdout=out, stderr=err
        )
        try:
            try:
                code = process.wait(timeout=args.session_timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.terminate()
                try:
                    code = process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    code = process.wait()
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
    result["session"] = classify_execution(
        code, timed_out, (args.out / "session.stdout").read_text(), expected
    )
    result["session"]["seconds"] = time.monotonic() - start
    write_json(args.out / "result.json", result)
    # Bounded model stage uses an exact native prefix, not invented push/pop.
    count = min(args.prefix_queries, len(expected))
    with path.open("rb") as stream:
        prefix = stream.read(traversal["query_ends"][count - 1]).decode()
    if "unknown" in expected[:count]:
        result["model_prefix"] = {"outcome": "oracle-unavailable", "queries": []}
    else:
        result["model_prefix"] = validate(
            prefix, args.out / "model-prefix", args, expected_answers=expected[:count]
        )
    result["inputs_unchanged"] = digest(path) == entry["sha256"]
    result["binaries_unchanged"] = all(
        digest(Path(shutil.which(command) or command)) == binaries[name]
        for name, command in {
            "solver": args.solver,
            "parser": args.parser_probe,
            "dolmen": args.dolmen,
        }.items()
    )
    write_json(args.out / "result.json", result)
    print(
        json.dumps(
            {
                "file": entry["file"],
                "parser_queries": len(expected),
                "session": result["session"]["outcome"],
                "model_prefix": result["model_prefix"]["outcome"],
            }
        )
    )
    return (
        0
        if (
            result["session"]["outcome"] == "pass"
            and result["model_prefix"]["outcome"] == "pass"
            and result["inputs_unchanged"]
            and result["binaries_unchanged"]
        )
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
