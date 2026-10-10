#!/usr/bin/env python3
"""Preserve independent diagnostics for retained-mutant error rows."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from run import digest, rows, write


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--shard", type=int, required=True)
    args = parser.parse_args()
    selected = {}
    for path in sorted((args.root / "retained-mutant-replay").glob("*/shard-*/answers.csv")):
        for row in rows(path):
            if row["outcome"] != "PASS" and "crash" in (row["ours"], row["reference"]):
                source = Path(row["file"]).resolve()
                selected[str(source)] = digest(source)
    out = args.root / "mutant-triage" / f"shard-{args.shard}"
    out.mkdir(parents=True, exist_ok=False)
    write(out / "selection.json", {"total_error_inputs": len(selected), "shard": args.shard,
                                    "inputs": dict(sorted(selected.items())[args.shard::4])})
    commands = {
        "dolmen": ["contrib/local/bin/dolmen", "--strict=false"],
        "default": ["build-slurm-rel/primo", "--quiet"],
        "both": ["build-slurm-rel/primo", "--quiet", "--lra-model-phase", "--monotone-elimination"],
        "z3": ["contrib/local/bin/z3"],
        "cvc5": ["contrib/local/bin/cvc5", "--lang=smt2"],
    }
    for index, (source, checksum) in enumerate(sorted(selected.items())[args.shard::4]):
        record = {"file": source, "sha256": checksum, "commands": {}}
        for name, command in commands.items():
            try:
                result = subprocess.run([*command, source], capture_output=True, text=True,
                                        timeout=10, check=False)
                record["commands"][name] = {"command": command, "exit": result.returncode,
                                            "stdout": result.stdout, "stderr": result.stderr}
            except subprocess.TimeoutExpired:
                record["commands"][name] = {"command": command, "timeout": True}
        write(out / f"{index}.json", record)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
