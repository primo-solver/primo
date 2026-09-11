#!/usr/bin/env python3
"""Replay a hash-checked native prefix with actual query models and recorded status oracles."""

import argparse
import json
import subprocess
from pathlib import Path

from check_native_incremental import declared_answers, digest
from validate_incremental import validate, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--prior", type=Path, required=True)
    parser.add_argument("--index", type=int, required=True)
    parser.add_argument("--queries", type=int, default=4)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--dolmen", required=True)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--dolmen-timeout", type=float, default=30)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.ref = "unused-recorded-native-status-oracle"
    args.ref_arg = []
    args.chunk_bytes = 4093
    entry = json.loads(args.manifest.read_text())["entries"][args.index]
    source = args.root / entry["file"]
    prior = args.prior / f"task-{args.index}"
    old = json.loads((prior / "run.json").read_text())
    result = json.loads((prior / "result.json").read_text())
    if old["entry"] != entry or digest(source) != entry["sha256"]:
        raise ValueError("native source identity mismatch")
    if result["parser"]["outcome"] != "pass" or not result["inputs_unchanged"]:
        raise ValueError("prior parser run is unqualified")
    offsets = json.loads((prior / "parser.stdout").read_text())["query_ends"]
    if not 0 < args.queries <= len(offsets):
        raise ValueError("invalid prefix query count")
    with source.open("rb") as stream:
        prefix = stream.read(offsets[args.queries - 1]).decode()
    expected = json.loads((prior / "oracle.json").read_text())[: args.queries]
    if expected != declared_answers(source, offsets[: args.queries]):
        raise ValueError("recorded native oracle mismatch")
    version = subprocess.check_output([args.solver, "--version"], text=True)
    if f"git: {args.expected_git}" not in version.splitlines():
        raise ValueError("candidate revision mismatch")
    args.out.mkdir(parents=True, exist_ok=False)
    write_json(
        args.out / "run.json",
        {
            "entry": entry,
            "version": version,
            "binary_sha256": digest(Path(args.solver)),
            "manifest_sha256": digest(args.manifest),
            "oracle_sha256": digest(prior / "oracle.json"),
            "queries": args.queries,
            "scope": "Bounded prefix only; no full-session qualification or timing claim",
        },
    )
    result = validate(prefix, args.out / "validation", args, expected_answers=expected)
    write_json(args.out / "result.json", result)
    print(
        json.dumps(
            {
                "index": args.index,
                "outcome": result["outcome"],
                "queries": len(result["queries"]),
            }
        )
    )
    return int(result["outcome"] != "pass")


if __name__ == "__main__":
    raise SystemExit(main())
