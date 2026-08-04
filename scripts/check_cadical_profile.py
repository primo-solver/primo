#!/usr/bin/env python3
"""Compare Primo's default CaDiCaL options with an incremental SAT profile.

This is a campaign guard for CaDiCaL-version regressions.  It does not replace
full solver benchmarks; it records whether a selected workload is sensitive to
CaDiCaL options that affect repeated incremental solving.
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import time
from pathlib import Path

INCREMENTAL_PROFILE = [
    "ilb=2",
    "sweep=0",
    "congruence=0",
    "backbone=0",
    "condition=0",
    "elim=0",
    "vivify=0",
    "probe=0",
    "subsume=0",
    "transred=0",
    "walk=0",
]


def completed_output_text(output: str | bytes | None) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode(errors="replace")
    return output


def run(command: list[str], timeout: float) -> dict[str, object]:
    start = time.perf_counter()
    timed_out = False
    try:
        proc = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
        stdout = proc.stdout
        stderr = proc.stderr
        exit_code = proc.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        stdout = completed_output_text(exc.stdout)
        stderr = completed_output_text(exc.stderr)
        exit_code = 124
    seconds = time.perf_counter() - start
    first_line = stdout.strip().splitlines()[0] if stdout.strip() else ""
    answer = (
        first_line
        if first_line in {"sat", "unsat", "unknown", "unsupported"}
        else "error"
    )
    status = (
        "timeout"
        if timed_out
        else ("error" if exit_code != 0 and answer == "error" else "ok")
    )
    return {
        "status": status,
        "answer": answer,
        "seconds": f"{seconds:.6f}",
        "exit_code": exit_code,
        "stderr_head": " ".join(stderr.splitlines()[:2])[:240],
    }


def solver_version(solver: list[str]) -> str:
    proc = subprocess.run(
        [*solver, "--version"],
        check=False,
        text=True,
        capture_output=True,
        timeout=10,
    )
    return (proc.stdout + proc.stderr).strip().replace("\n", " | ")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", nargs="+", default=["./build/primo"])
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--out", type=Path, default=Path("results/cadical-profile.csv"))
    parser.add_argument(
        "--summary", type=Path, default=Path("results/cadical-profile.md")
    )
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()

    profiles = {
        "default": [],
        "incremental": [
            arg for opt in INCREMENTAL_PROFILE for arg in ("--cadical-option", opt)
        ],
    }
    rows: list[dict[str, object]] = []
    for file in args.files:
        for profile, profile_args in profiles.items():
            command = [*args.solver, "--quiet", *profile_args, str(file)]
            result = run(command, args.timeout)
            rows.append(
                {
                    "profile": profile,
                    "file": str(file),
                    "command": " ".join(command),
                    **result,
                }
            )
            print(
                f"{profile} {file}: {result['status']} {result['answer']} "
                f"{result['seconds']}s",
                flush=True,
            )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "profile",
                "file",
                "command",
                "status",
                "answer",
                "seconds",
                "exit_code",
                "stderr_head",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    with args.summary.open("w") as handle:
        handle.write("# CaDiCaL Profile Check\n\n")
        handle.write(f"- Solver: `{solver_version(args.solver)}`\n")
        handle.write(f"- Timeout: {args.timeout:g}s\n")
        handle.write(f"- Incremental profile: `{', '.join(INCREMENTAL_PROFILE)}`\n\n")
        handle.write("| profile | file | status | answer | seconds |\n")
        handle.write("|---|---|---|---|---:|\n")
        for row in rows:
            handle.write(
                f"| {row['profile']} | `{row['file']}` | {row['status']} | "
                f"{row['answer']} | {row['seconds']} |\n"
            )
    print(f"wrote {args.out}")
    print(f"wrote {args.summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
