#!/usr/bin/env python3
"""Replay a deterministic sample from each publication shard with conflict limit 1."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from validate_qfuf_models_with_dolmen import command_spans


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shard", type=Path, required=True)
    parser.add_argument("--slot", type=int, required=True)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--count", type=int, default=25)
    parser.add_argument("--timeout", type=float, default=10)
    args = parser.parse_args()
    gate = args.shard / "gate"
    if args.slot < 20:
        with (gate / "summary.csv").open() as handle:
            paths = [
                Path(row.get("case") or row["file"]) for row in csv.DictReader(handle)
            ]
    elif args.slot < 24:
        paths = sorted(gate.glob("case-*/input.smt2"))
    else:
        paths = sorted(gate.glob("session-*/original.smt2"))
    if len(paths) < args.count:
        raise SystemExit("missing replay inputs")
    output = args.shard / "budget-replay"
    output.mkdir(exist_ok=False)
    rows = []
    for index, original in enumerate(paths[: args.count]):
        directory = output / f"{index:05d}"
        directory.mkdir()
        text = original.read_text()
        adapted = "\n".join(
            text[a:b]
            for a, b, head in command_spans(text)
            if head[0] not in {"get-model", "get-info", "get-value", "exit"}
        )
        path = directory / "input.smt2"
        path.write_text(adapted + "\n")
        options = ["--quiet", "--stats", "--max-theory-conflicts=1"]
        if args.slot >= 24:
            options.append("--incremental-mode=assumptions")
        row = {
            "input": str(original),
            "sha256": hashlib.sha256(text.encode()).hexdigest(),
            "adapted_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "commands": [
                [args.reference, str(path)],
                [args.solver, *options, str(path)],
            ],
        }
        answers = []
        outputs = []
        for name, command in zip(("reference", "bounded"), row["commands"]):
            try:
                result = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=args.timeout,
                )
                stdout, stderr, code = result.stdout, result.stderr, result.returncode
            except subprocess.TimeoutExpired as error:
                stdout = (
                    (error.stdout or b"").decode()
                    if isinstance(error.stdout, bytes)
                    else (error.stdout or "")
                )
                stderr = (
                    (error.stderr or b"").decode()
                    if isinstance(error.stderr, bytes)
                    else (error.stderr or "")
                )
                code = "timeout"
            (directory / f"{name}.stdout").write_text(stdout)
            (directory / f"{name}.stderr").write_text(stderr)
            row[name + "_exit"] = code
            answers.append(re.findall(r"^(sat|unsat|unknown)$", stdout, re.MULTILINE))
            outputs.append(stderr)
        reference, bounded = answers
        row["reference_answers"], row["bounded_answers"] = answers
        counts = [
            int(value)
            for value in re.findall(
                r"^:theory\.conflicts (\d+)$", outputs[1], re.MULTILINE
            )
        ]
        row["conflicts"] = counts
        row["exhausted_queries"] = 0
        if any(row[name + "_exit"] == "timeout" for name in ("reference", "bounded")):
            row["outcome"] = "timeout"
        elif any(row[name + "_exit"] != 0 for name in ("reference", "bounded")):
            row["outcome"] = "error"
        elif (
            not reference
            or len(bounded) != len(reference)
            or len(counts) != len(bounded)
        ):
            row["outcome"] = "missing-results"
        elif any(expected == "unknown" for expected in reference):
            row["outcome"] = "reference-incomplete"
        else:
            row["outcome"] = "pass"
            for expected, actual, count in zip(reference, bounded, counts):
                if actual == "unknown" and count >= 1:
                    row["exhausted_queries"] += 1
                elif actual != expected or count >= 1:
                    row["outcome"] = "wrong-answer-or-limit"
            if row["outcome"] == "pass" and row["exhausted_queries"]:
                row["outcome"] = "resource-exhausted"
        rows.append(row)
        (directory / "result.json").write_text(json.dumps(row, indent=2) + "\n")
    summary = {
        "count": len(rows),
        "timeout_seconds": args.timeout,
        "outcomes": dict(Counter(row["outcome"] for row in rows)),
        "exhausted_queries": sum(row["exhausted_queries"] for row in rows),
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))
    return int(
        any(
            row["outcome"]
            not in {"pass", "resource-exhausted", "timeout", "reference-incomplete"}
            for row in rows
        )
    )


if __name__ == "__main__":
    raise SystemExit(main())
