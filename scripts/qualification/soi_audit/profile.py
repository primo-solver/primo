#!/usr/bin/env python3
"""Profile an unchanged qualified binary on SLURM; instrumentation is diagnostic."""

import argparse
import hashlib
import json
import os
import platform
import socket
import subprocess
import time
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, stem, seconds):
    # GNU timeout owns the subprocess group, including profiler descendants.
    argv = ["timeout", "--signal=TERM", "--kill-after=5", str(seconds), *command]
    start = time.monotonic()
    with (
        stem.with_suffix(".stdout").open("wb") as out,
        stem.with_suffix(".stderr").open("wb") as err,
    ):
        result = subprocess.run(argv, stdout=out, stderr=err, check=False)
    answers = [
        line.strip()
        for line in stem.with_suffix(".stdout").read_text().splitlines()
        if line.strip() in ("sat", "unsat", "unknown")
    ]
    return {
        "command": argv,
        "returncode": result.returncode,
        "seconds": time.monotonic() - start,
        "answers": answers,
        "complete": result.returncode == 0 and len(answers) == 1,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", type=int, required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        raise SystemExit("solver profiling requires a SLURM allocation")
    plan_path = Path(__file__).with_name("plan.json")
    plan = json.loads(plan_path.read_text())
    case = plan["cases"][args.case]
    source = args.corpus / case["file"]
    if digest(args.solver) != plan["binary_sha256"] or digest(source) != case["sha256"]:
        raise SystemExit("binary/input identity mismatch")
    out = args.out / ("case-" + str(args.case))
    out.mkdir(parents=True, exist_ok=False)
    metadata = {
        "host": socket.gethostname(),
        "job": os.environ["SLURM_JOB_ID"],
        "python": platform.python_version(),
        "case": case,
        "binary_sha256": digest(args.solver),
        "plan_sha256": digest(plan_path),
        "driver_sha256": digest(Path(__file__)),
        "runs": [],
    }
    failed = False
    for config, rule, fixed in (
        ("sparse-g1-f0", "sparse", False),
        ("soi-g1-f0", "soi", False),
        ("soi-g1-f1", "soi", True),
    ):
        command = [
            str(args.solver),
            "--quiet",
            "--stats",
            "--lra-pivoting-rule",
            rule,
            "--gaussian-elimination",
        ]
        if fixed:
            command.append("--lra-fixed-elimination")
        command.append(str(source))
        native = run(
            command, out / (config + "-native"), plan["native_timeout_seconds"]
        )
        failed |= not native["complete"] or native["answers"] != [case["expected"]]
        instrumented = run(
            [
                "valgrind",
                "--tool=callgrind",
                "--callgrind-out-file=" + str(out / (config + ".callgrind")),
                *command,
            ],
            out / (config + "-instrumented"),
            plan["callgrind_timeout_seconds"],
        )
        # A bounded partial profile is diagnostic; never count it as solved.
        if instrumented["returncode"] not in (0, 124, 137):
            failed = True
        if instrumented["complete"] and instrumented["answers"] != [case["expected"]]:
            failed = True
        for inclusive in ("yes", "no"):
            with (out / (config + "-inclusive-" + inclusive + ".txt")).open(
                "w"
            ) as handle:
                annotation = subprocess.run(
                    [
                        "callgrind_annotate",
                        "--inclusive=" + inclusive,
                        "--threshold=99",
                        str(out / (config + ".callgrind")),
                    ],
                    stdout=handle,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
            if annotation.returncode:
                failed = True
        metadata["runs"].append(
            {"configuration": config, "native": native, "instrumented": instrumented}
        )
        (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print(config, native["answers"], instrumented["returncode"], flush=True)
    metadata["binary_unchanged"] = digest(args.solver) == plan["binary_sha256"]
    metadata["accepted"] = not failed and metadata["binary_unchanged"]
    (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return 0 if metadata["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
