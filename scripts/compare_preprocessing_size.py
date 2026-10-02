#!/usr/bin/env python3
"""Compare the *size of the preprocessed formula* between two Primo builds.

This is deliberately not a performance comparison. It runs each solver with
``--preprocess-only --stats``, which parses and preprocesses ground input and
skips search. The response is ``unsat`` if preprocessing proves a contradiction,
otherwise ``unknown``. Quantified input stops after the frontend. The counters it reports are therefore a
deterministic function of the input and the binary: no scheduler, no timeout
boundary, no TermId-order-dependent search. Two runs of the same binary on the
same file give byte-identical numbers.

That is what makes it the right instrument for separating a *structural*
encoding regression from timeout-boundary churn. A change that makes the
preprocessed formula bigger shows up here with no noise to argue about; a
change that only shifts the search shows up here as no difference at all.

Written for the question "why did making ite a term kind lose 79 QF_LIA files",
where the losses concentrate in one ite-heavy family (``nec-smt``) and the
paired runtime ratio on that family is 1.14 while it is 1.00 everywhere else.

The auxiliary counter moved name between the two builds being compared
(``:frontend.ite.auxiliaries`` before ite became a term kind,
``:preprocessing.ite.auxiliaries`` after), so both spellings are accepted and
recorded in one column.

Example
-------
    python3 scripts/compare_preprocessing_size.py \
        --root sandbox/non-incremental/QF_LIA/nec-smt \
        --solver base=/path/to/old/primo \
        --solver head=build/primo \
        --out results/preproc-size

Sharding matches bench_qf_uf_full.py: files are discovered in sorted order and
partitioned by ``index % shards == shard_index``, so shard N of every solver
sees exactly the same files.
"""

from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
import time
from pathlib import Path

# Counters recorded per file. Each entry is (column, [counter names to try]).
# The first name that the binary reported wins, so a counter that was renamed
# between the two builds still lands in one column.
COLUMNS: list[tuple[str, list[str]]] = [
    ("input_assertions", [":input.assertions"]),
    ("preprocessed_assertions", [":preprocessing.assertions"]),
    ("term_size_initial", [":preprocessing.term-size.initial"]),
    ("term_size_final", [":preprocessing.term-size.final"]),
    ("ite_auxiliaries", [":preprocessing.ite.auxiliaries", ":frontend.ite.auxiliaries"]),
    ("ite_boolean", [":preprocessing.ite.boolean", ":frontend.ite.boolean"]),
    ("ite_rewrites", [":preprocessing.ite.rewrites"]),
    ("ite_equality_distributed", [":preprocessing.ite.equality-distributed"]),
    ("real_equality_lowering", [":preprocessing.real-equality-lowering.lowered"]),
]

FIELDNAMES = ["file", "family", "exit_code", "seconds"] + [name for name, _ in COLUMNS]


def discover(root: Path) -> list[Path]:
    return sorted(p for p in root.rglob("*.smt2") if p.is_file())


def family_of(path: Path, root: Path, override: str) -> str:
    # Pointing --root at a family directory makes the first component below it
    # a size bucket rather than a family (nec-smt/large/...), so --family
    # overrides rather than guessing.
    if override:
        return override
    try:
        relative = path.relative_to(root)
    except ValueError:
        return root.name
    return relative.parts[0] if len(relative.parts) > 1 else root.name


def parse_stats(text: str) -> dict[str, str]:
    stats: dict[str, str] = {}
    for line in text.splitlines():
        if not line.startswith(":"):
            continue
        parts = line.split(None, 1)
        if len(parts) == 2:
            stats[parts[0]] = parts[1].strip()
    return stats


def run_one(command: list[str], path: Path, timeout: int) -> dict[str, object]:
    started = time.monotonic()
    try:
        completed = subprocess.run(
            command + ["--quiet", "--stats", "--preprocess-only", str(path)],
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        elapsed = time.monotonic() - started
        # --stats writes counters to stderr; stdout is unknown, or unsat
        # when preprocessing proves a contradiction.
        stats = parse_stats(completed.stderr)
        exit_code = completed.returncode
    except subprocess.TimeoutExpired:
        return {"exit_code": "timeout", "seconds": f"{timeout:.3f}"}
    except OSError as error:  # pragma: no cover - operational failure
        return {"exit_code": f"error:{error.errno}", "seconds": "0"}

    row: dict[str, object] = {"exit_code": exit_code, "seconds": f"{elapsed:.3f}"}
    for column, names in COLUMNS:
        for name in names:
            if name in stats:
                row[column] = stats[name]
                break
        else:
            row[column] = ""
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument(
        "--solver",
        action="append",
        required=True,
        metavar="NAME=COMMAND",
        help="repeatable; COMMAND may carry its own flags",
    )
    parser.add_argument("--out", required=True, type=Path, help="output directory")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--limit", type=int, default=0, help="0 means no limit")
    parser.add_argument("--family", default="", help="override the recorded family name")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    if not root.is_dir():
        print(f"root {root} is not a directory", file=sys.stderr)
        return 2

    files = discover(root)
    if arguments.limit:
        files = files[: arguments.limit]
    shard = [p for index, p in enumerate(files) if index % arguments.shards == arguments.shard_index]
    print(f"{len(files)} files under {root}; shard {arguments.shard_index}/{arguments.shards} takes {len(shard)}")

    arguments.out.mkdir(parents=True, exist_ok=True)
    for spec in arguments.solver:
        name, separator, command_str = spec.partition("=")
        if not separator or not name:
            print(f"invalid --solver {spec!r}; expected NAME=COMMAND", file=sys.stderr)
            return 2
        command = command_str.split()
        if not os.access(command[0], os.X_OK):
            print(f"solver {name}: {command[0]} is not executable", file=sys.stderr)
            return 2

        destination = arguments.out / f"{name}-shard-{arguments.shard_index}.csv"
        with destination.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=FIELDNAMES)
            writer.writeheader()
            for index, path in enumerate(shard, start=1):
                row = run_one(command, path, arguments.timeout)
                row["file"] = str(path.relative_to(root.parent))
                row["family"] = family_of(path, root, arguments.family)
                writer.writerow(row)
                if index % 200 == 0:
                    handle.flush()
                    print(f"  {name}: {index}/{len(shard)}", flush=True)
        print(f"wrote {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
