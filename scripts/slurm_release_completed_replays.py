#!/usr/bin/env python3
"""Release stale replay dependencies only after verified paired timing completion.

This is an opt-in workaround for aftercorr tasks remaining pending after their
matching source task completed. It does not submit, cancel, or rerun jobs.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
from pathlib import Path


def output(command: list[str]) -> str:
    return subprocess.check_output(command, text=True)


def completed_indices(records: str, job: str) -> set[int]:
    completed = set()
    for line in records.splitlines():
        fields = line.strip().split("|")
        if len(fields) != 3:
            continue
        match = re.fullmatch(re.escape(job) + r"_(\d+)", fields[0])
        if match and fields[1:] == ["COMPLETED", "0:0"]:
            completed.add(int(match[1]))
    return completed


def validate_timing(root: Path, job: str, index: int, identities: dict[str, str],
                    timeout: float, solver_args: list[str]) -> None:
    expected_inputs = None
    expected_shards = None
    for rnd in (0, 1):
        for arm in ("baseline", "candidate"):
            path = root / f"round-{rnd}" / arm / f"shard-{index}"
            metadata = json.loads((path / "provenance.json").read_text())
            if (metadata["status"] != "complete" or not metadata["inputs_unchanged"]
                    or metadata["run_id"] != f"component-ab-{job}-{rnd}-{arm}-{index}"
                    or metadata["shard_index"] != index
                    or metadata["solver"]["sha256"] != identities[arm]
                    or metadata["timeout"] != timeout or metadata["solver_args"] != solver_args):
                raise ValueError(f"unexpected or incomplete timing provenance: {path}")
            inputs = metadata["inputs"]
            shards = metadata["shards"]
            if (not inputs or not 0 <= index < shards
                    or expected_inputs is not None and inputs != expected_inputs
                    or expected_shards is not None and shards != expected_shards):
                raise ValueError(f"inconsistent timing input/shard identity: {path}")
            expected_inputs, expected_shards = inputs, shards
            with (path / "answers.csv").open() as stream:
                rows = list(csv.DictReader(stream))
            if len(rows) != len(inputs) or {row["file"] for row in rows} != set(inputs):
                raise ValueError(f"incomplete timing rows: {path}")
            if any(row["status"] not in {"ok", "timeout"}
                   or row.get("wrong") != "false" for row in rows):
                raise ValueError(f"timing correctness issue requires triage: {path}")


def plan_releases(root: Path, timing_job: str, replay_jobs: list[str],
                  identities: dict[str, str], timeout: float, solver_args: list[str],
                  query=output) -> list[str]:
    if any(not re.fullmatch(r"\d+", job) for job in [timing_job, *replay_jobs]):
        raise ValueError("expected numeric SLURM array IDs")
    if len(set(replay_jobs)) != len(replay_jobs) or timing_job in replay_jobs:
        raise ValueError("timing and replay array IDs must be distinct")
    complete = completed_indices(query([
        "sacct", "-n", "-X", "-P", "-j", timing_job, "--format=JobID,State,ExitCode",
    ]), timing_job)
    verified = set()
    actions = []
    for replay_job in replay_jobs:
        pending = query(["squeue", "-r", "-h", "-j", replay_job, "-t", "PD", "-o", "%i"])
        for name in pending.splitlines():
            name = name.strip()
            match = re.fullmatch(re.escape(replay_job) + r"_(\d+)", name)
            if not match or int(match[1]) not in complete:
                continue
            index = int(match[1])
            if index not in verified:
                validate_timing(root, timing_job, index, identities, timeout, solver_args)
                verified.add(index)
            actions.append(name)
    return actions


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--timing-job", required=True)
    parser.add_argument("--replay-job", action="append", required=True)
    parser.add_argument("--baseline-sha256", required=True)
    parser.add_argument("--candidate-sha256", required=True)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--release", action="store_true", help="Apply verified dependency removals")
    args = parser.parse_args()
    actions = plan_releases(
        args.root, args.timing_job, args.replay_job,
        {"baseline": args.baseline_sha256, "candidate": args.candidate_sha256},
        args.timeout, ["--quiet", "--stats", *args.solver_arg],
    )
    print(json.dumps({"mode": "release" if args.release else "dry-run", "verified": actions}),
          flush=True)
    if args.release:
        for job in actions:
            # A task may have started naturally since the read-only plan.
            if output(["squeue", "-h", "-j", job, "-o", "%T"]).strip() != "PENDING":
                continue
            subprocess.run(["scontrol", "update", f"JobId={job}", "Dependency="], check=True)
            print(json.dumps({"released": job}), flush=True)


if __name__ == "__main__":
    main()
