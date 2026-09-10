#!/usr/bin/env python3
"""Real-corpus stdin/scoping checks using hash-pinned SMT-LIB status oracles."""

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from validate_incremental import command_spans, run_command, validate, write_json


def scoped_session(text, expected, logic):
    """Preserve source declarations/assertions; add only independent Bool scopes."""
    declarations = []
    metadata = []
    seen_query = False
    seen_logic = False
    seen_status = False
    for start, end, head in command_spans(text):
        kind = head[0]
        raw = text[start:end]
        if seen_query:
            if kind not in {"get-model", "exit"}:
                raise ValueError("unsupported post-query command")
        elif kind == "check-sat" and head == ("check-sat",):
            seen_query = True
        elif kind == "set-logic" and head == ("set-logic", logic) and not seen_logic:
            seen_logic = True
        elif kind == "set-info":
            if head[1] == ":status":
                if head != ("set-info", ":status", expected):
                    raise ValueError("declared status differs from manifest")
                seen_status = True
            else:
                metadata.append(raw)
        elif kind in {
            "declare-sort",
            "declare-const",
            "declare-fun",
            "define-fun",
            "assert",
        }:
            declarations.append(raw)
        elif kind == "set-option" and head[1] in {":produce-models", ":print-success"}:
            pass
        else:
            raise ValueError(f"unsupported corpus command: {head}")
    if not (seen_logic and seen_query and seen_status):
        raise ValueError("missing unique logic/query or known declared status")
    fresh = "$primo_incremental_probe"
    while fresh in text:
        fresh += "_"
    commands = [
        f"(set-logic {logic})",
        *metadata,
        "(push 1)",
        *declarations,
        "(check-sat)",
        "(push 1)",
        f"(declare-const {fresh} Bool)",
        f"(assert {fresh})",
        "(check-sat)",
        "(push 1)",
        f"(assert (not {fresh}))",
        "(check-sat)",
        "(pop 1)",
        "(check-sat)",
        "(pop 1)",
        "(push 1)",
        f"(declare-const {fresh} Bool)",
        f"(assert (not {fresh}))",
        "(check-sat)",
        "(pop 1)",
        "(pop 1)",
        "(check-sat)",
        "(push 1)",
        *declarations,
        "(check-sat)",
        "(pop 1)",
        "(exit)",
    ]
    return "\n".join(commands) + "\n", [
        expected,
        expected,
        "unsat",
        expected,
        expected,
        "sat",
        expected,
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--logic", required=True)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--dolmen", default="dolmen")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--dolmen-timeout", type=float, default=30)
    parser.add_argument("--chunk-bytes", type=int, default=127)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.chunk_bytes < 1 or args.timeout <= 0 or args.dolmen_timeout <= 0:
        parser.error("budgets and chunk size must be positive")
    args.solver_arg = ["--quiet", "--max-theory-conflicts=1000"]
    args.ref, args.ref_arg = "unused-recorded-oracle", []
    version = subprocess.run(
        [args.solver, "--version"], capture_output=True, text=True, check=True
    ).stdout
    if f"git: {args.expected_git}" not in version.splitlines():
        parser.error("candidate revision mismatch")
    args.out.mkdir(parents=True, exist_ok=False)
    binary_hash = hashlib.sha256(Path(args.solver).read_bytes()).hexdigest()
    dolmen_version = subprocess.run(
        [args.dolmen, "--version"], capture_output=True, text=True, check=True
    ).stdout
    write_json(
        args.out / "run.json",
        {
            "arguments": {
                k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()
            },
            "solver_version": version,
            "solver_sha256": binary_hash,
            "dolmen_version": dolmen_version,
            "dolmen_sha256": hashlib.sha256(
                Path(shutil.which(args.dolmen) or args.dolmen).read_bytes()
            ).hexdigest(),
            "manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
            "oracle": "hash-pinned SMT-LIB declared status, agreeing with recorded Primo result; metamorphic queries follow scoped_session; no reference solver calls",
        },
    )
    rows = []
    entries = [
        e
        for e in json.loads(args.manifest.read_text())["entries"]
        if e["logic"] == args.logic
    ]
    if not entries:
        parser.error("empty logic selection")
    for index, entry in enumerate(entries):
        directory = args.out / f"case-{index:03d}"
        directory.mkdir()
        row = dict(entry, outcome="pending")
        rows.append(row)
        try:
            path = args.root / entry["file"]
            data = path.read_bytes()
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise ValueError("input hash mismatch")
            text = data.decode()
            (directory / "original.smt2").write_bytes(data)
            adapted, answers = scoped_session(text, entry["expected"], args.logic)
            # File-path solve is a separate baseline, with exit checked before answer.
            status, code, stdout, stderr, _ = run_command(
                [args.solver, *args.solver_arg, str(path)], args.timeout
            )
            (directory / "file.stdout").write_text(stdout)
            (directory / "file.stderr").write_text(stderr)
            if status != "done" or code != 0 or stdout.strip() != entry["expected"]:
                raise ValueError(
                    f"file baseline failed: {status}, exit={code}, {stdout!r}"
                )
            row["file_answer"] = stdout.strip()
            for label, source, expected in (
                ("stdin", text, [entry["expected"]]),
                ("scoped", adapted, answers),
            ):
                result = validate(
                    source, directory / label, args, expected_answers=expected
                )
                row[label] = result
                if result["outcome"] != "pass":
                    raise ValueError(f"{label}: {result.get('message')}")
            row["outcome"] = "pass"
        except (OSError, ValueError) as error:
            row["outcome"], row["message"] = "failure", str(error)
        write_json(args.out / "summary.json", rows)
        if row["outcome"] != "pass":
            print(json.dumps(row))
            return 1
    if hashlib.sha256(Path(args.solver).read_bytes()).hexdigest() != binary_hash:
        raise ValueError("solver binary changed during run")
    print(
        json.dumps(
            {
                "files": len(rows),
                "sessions": 2 * len(rows),
                "queries": 8 * len(rows),
                "outcome": "pass",
            }
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
