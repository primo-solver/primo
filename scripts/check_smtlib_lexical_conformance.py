#!/usr/bin/env python3
"""Audit original lexical cases against explicit standard expectations, not votes."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shutil
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def classify(case, returncode, stdout, stderr, checker=False):
    answers = re.findall(r"(?m)^\s*(sat|unsat|unknown)\s*$", stdout)
    if returncode is None:
        return "timeout", answers
    if returncode < 0:
        return "crash", answers
    diagnostics = (stdout + "\n" + stderr).lower()
    rejected = bool(re.search(r"\(error\b|\berror\b|invalid token", diagnostics))
    if not rejected and "unsupported" in diagnostics:
        return "inconclusive", answers
    if returncode != 0 and not rejected:
        return "process-error", answers
    if case["valid"] is None:
        return "diagnostic-rejected" if rejected else "diagnostic-accepted", answers
    if rejected:
        return "valid-rejected" if case["valid"] else "invalid-rejected", answers
    if not case["valid"]:
        return "invalid-accepted", answers
    if checker:
        return "valid-accepted", answers
    if "unknown" in answers or "unsupported" in diagnostics:
        return "inconclusive", answers
    if answers == case["answers"]:
        return "answer-pass", answers
    if len(answers) == len(case["answers"]):
        return "wrong-answer", answers
    return "response-mismatch", answers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cases", type=Path, default=Path("benchmarks/lexical-conformance/cases.json")
    )
    parser.add_argument("--primo", default="build/primo")
    parser.add_argument("--bin-dir", type=Path, default=Path("contrib/local/bin"))
    parser.add_argument("--dolmen", default="dolmen")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--only", nargs="+", help="Run only named profiles")
    args = parser.parse_args()
    cases = json.loads(args.cases.read_text())["cases"]
    profiles = {
        "primo": [args.primo, "--quiet"],
        "z3": [str(args.bin_dir / "z3"), "-smt2"],
        "cvc5": [str(args.bin_dir / "cvc5"), "--lang=smt2.6", "--incremental"],
        "cvc5-strict": [
            str(args.bin_dir / "cvc5"),
            "--lang=smt2.6",
            "--incremental",
            "--strict-parsing",
        ],
        "yices": [str(args.bin_dir / "yices-smt2"), "--incremental"],
        "opensmt": [str(args.bin_dir / "opensmt")],
        "dolmen": [args.dolmen, "--input=smt2.6", "--strict=true"],
    }
    if args.only:
        if set(args.only) - profiles.keys():
            parser.error("unknown profile in --only")
        profiles = {name: profiles[name] for name in args.only}
    identities = {}
    for name, command in profiles.items():
        binary = shutil.which(command[0])
        if binary is None:
            parser.error(f"missing tool: {command[0]}")
        command[0] = str(Path(binary).resolve())
        version = subprocess.run(
            [command[0], "--version"],
            capture_output=True,
            text=True,
            timeout=args.timeout,
            check=True,
        )
        identities[name] = {
            "command": command,
            "sha256": digest(Path(command[0])),
            "version": version.stdout + version.stderr,
        }
    args.out.mkdir(parents=True, exist_ok=False)
    metadata = {
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "git_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], text=True
        ).strip(),
        "worktree_status": subprocess.check_output(
            ["git", "status", "--short"], text=True
        ),
        "argv": sys.argv,
        "timeout": args.timeout,
        "cases": len(cases),
        "cases_sha256": digest(args.cases),
        "script_sha256": digest(Path(__file__)),
        "profiles": identities,
        "note": "Correctness only; Dolmen is a strict parser/type checker here, not a SAT oracle or model check.",
    }
    (args.out / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    rows = []
    for case in cases:
        folder = args.out / case["id"]
        folder.mkdir()
        input_file = folder / "input.smt2"
        input_file.write_bytes(case["input"].encode("utf-8"))
        for name, command in profiles.items():
            invocation = [*command, str(input_file.resolve())]
            try:
                proc = subprocess.run(
                    invocation, capture_output=True, timeout=args.timeout, check=False
                )
                returncode, stdout, stderr = proc.returncode, proc.stdout, proc.stderr
            except subprocess.TimeoutExpired as error:
                returncode, stdout, stderr = (
                    None,
                    error.stdout or b"",
                    error.stderr or b"",
                )
            (folder / f"{name}.stdout").write_bytes(stdout)
            (folder / f"{name}.stderr").write_bytes(stderr)
            outcome, answers = classify(
                case,
                returncode,
                stdout.decode(errors="replace"),
                stderr.decode(errors="replace"),
                name == "dolmen",
            )
            rows.append(
                {
                    "case": case["id"],
                    "group": case["group"],
                    "profile": name,
                    "valid": case["valid"],
                    "expected": case["answers"],
                    "answers": answers,
                    "returncode": returncode,
                    "outcome": outcome,
                    "command": invocation,
                    "input_sha256": digest(input_file),
                }
            )
    unchanged = all(
        digest(Path(meta["command"][0])) == meta["sha256"]
        for meta in identities.values()
    )
    summary = {
        name: dict(Counter(row["outcome"] for row in rows if row["profile"] == name))
        for name in profiles
    }
    summary["binaries_unchanged"] = unchanged
    (args.out / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    with (args.out / "matrix.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["case", "valid", "expected", *profiles])
        for case in cases:
            selected = {
                row["profile"]: row["outcome"]
                for row in rows
                if row["case"] == case["id"]
            }
            writer.writerow(
                [
                    case["id"],
                    case["valid"],
                    " ".join(case["answers"]),
                    *(selected[name] for name in profiles),
                ]
            )
    print(json.dumps(summary, indent=2))
    passing = {
        "answer-pass",
        "valid-accepted",
        "invalid-rejected",
        "diagnostic-accepted",
        "diagnostic-rejected",
    }
    return 0 if unchanged and all(row["outcome"] in passing for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
