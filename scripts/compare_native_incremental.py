#!/usr/bin/env python3
"""Bounded timing diagnostic on unchanged native sessions; not a model gate."""

import argparse
import json
import platform
import shutil
import subprocess
import time
from pathlib import Path

from check_native_incremental import classify_execution, declared_answers, digest
from validate_incremental import write_json


def run_session(command, source, out, timeout):
    start = time.monotonic()
    timed_out = False
    with (
        source.open("rb") as stdin,
        (out / "stdout").open("wb") as stdout,
        (out / "stderr").open("wb") as stderr,
    ):
        try:
            code = subprocess.run(
                command, stdin=stdin, stdout=stdout, stderr=stderr, timeout=timeout, check=False
            ).returncode
        except subprocess.TimeoutExpired:
            # subprocess.run kills and reaps the direct solver before raising.
            timed_out, code = True, None
    return code, timed_out, time.monotonic() - start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--prior-out", type=Path, required=True)
    parser.add_argument("--index", type=int, choices=(0, 1), required=True)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--label", choices=("primo", "z3", "cvc5"), required=True)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    entry = json.loads(args.manifest.read_text())["entries"][args.index]
    source = args.root / entry["file"]
    prior = args.prior_out / f"task-{args.index}"
    provenance = json.loads((prior / "run.json").read_text())
    previous = json.loads((prior / "result.json").read_text())
    if provenance["entry"] != entry or digest(source) != entry["sha256"]:
        raise ValueError("source/prior entry identity mismatch")
    if previous["parser"]["outcome"] != "pass" or not previous["inputs_unchanged"]:
        raise ValueError("prior parser traversal is not qualified")
    expected = json.loads((prior / "oracle.json").read_text())
    offsets = json.loads((prior / "parser.stdout").read_text())["query_ends"]
    if len(expected) != entry["queries"] or expected != declared_answers(source, offsets):
        raise ValueError("oracle count/content mismatch")
    solver = Path(shutil.which(args.solver) or args.solver).resolve()
    version = subprocess.run(
        [str(solver), "--version"], capture_output=True, text=True, check=True, timeout=10
    ).stdout
    if args.label == "primo" and f"git: {args.expected_git}" not in version.splitlines():
        raise ValueError("candidate revision mismatch")
    args.out.mkdir(parents=True, exist_ok=False)
    binary_hash = digest(solver)
    command = [str(solver), *args.solver_arg]
    write_json(args.out / "run.json", {
        "entry": entry, "label": args.label, "command": command,
        "version": version, "binary_sha256": binary_hash,
        "manifest_sha256": digest(args.manifest),
        "oracle_sha256": digest(prior / "oracle.json"),
        "driver_sha256": digest(Path(__file__)),
        "timeout_seconds": args.timeout, "host": platform.node(),
        "candidate_revision": args.expected_git,
        "scope": "Untouched stdin; bounded diagnostic, no model generation/validation added. No per-query timing claim.",
    })
    code, timed_out, seconds = run_session(command, source, args.out, args.timeout)
    result = classify_execution(code, timed_out, (args.out / "stdout").read_text(), expected)
    result.update({
        "seconds": seconds, "label": args.label, "file": entry["file"],
        "inputs_unchanged": digest(source) == entry["sha256"],
        "binary_unchanged": digest(solver) == binary_hash,
        "model_validation": "not run; separate first-four-query prior sample only",
    })
    write_json(args.out / "result.json", result)
    print(json.dumps({k: result[k] for k in ("label", "file", "outcome", "seconds")}))
    return int(
        not result["inputs_unchanged"] or not result["binary_unchanged"]
        or bool(result["mismatches"])
        or result["outcome"] not in {"pass", "complete-with-unverified-answers", "timeout", "unknown"}
    )


if __name__ == "__main__":
    raise SystemExit(main())
