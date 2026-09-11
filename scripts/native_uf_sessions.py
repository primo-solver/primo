#!/usr/bin/env python3
"""Inventory, time and independently validate the native incremental UF archive.

Timing consumes unchanged files through stdin. Validation is a separate replay;
neither partial query progress nor an unchecked SAT model qualifies a session.
"""

import argparse
import hashlib
import json
import os
import platform
import signal
import subprocess
import tarfile
import time
from pathlib import Path, PurePosixPath

from validate_incremental import command_spans, validate, write_json

ARCHIVE_SHA256 = "52c55a17ee15e5c4a0fc723b9605ae42615da34499efc7e5a5e8b2c0ffc26a27"


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def run_session(command, source, out, timeout):
    start = time.monotonic()
    timed_out = False
    with (source.open("rb") as stdin, (out / "stdout").open("wb") as stdout,
          (out / "stderr").open("wb") as stderr):
        process = subprocess.Popen(
            ["/usr/bin/time", "-f", "%M", "-o", str(out / "maxrss-kib"), *command],
            stdin=stdin, stdout=stdout, stderr=stderr, start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out, code = True, None
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    return code, timed_out, time.monotonic() - start


def query_metadata(text):
    status = "unknown"
    answers = []
    counts = {"push": 0, "pop": 0}
    for _, _, head in command_spans(text):
        if head[:2] == ("set-info", ":status"):
            status = head[2]
        elif head == ("check-sat",):
            answers.append(status)
            status = "unknown"  # Never infer the next query's annotation.
        elif head[0] in counts:
            counts[head[0]] += int(head[1])
        elif head[0] == "check-sat-assuming":
            raise ValueError("unsupported native query")
        elif head[0] in {"reset", "reset-assertions"}:
            status = "unknown"
    return {"answers": answers, "queries": len(answers), **counts}


def inventory(archive, root, manifest):
    if digest(archive) != ARCHIVE_SHA256:
        raise ValueError("native UF archive checksum mismatch")
    root.mkdir(parents=True, exist_ok=False)
    entries = []
    process = subprocess.Popen(["zstd", "-dc", str(archive)], stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=process.stdout, mode="r|") as tar:
            for member in tar:
                if member.isdir():
                    continue
                path = PurePosixPath(member.name)
                if (not member.isfile() or path.is_absolute() or ".." in path.parts
                        or path.suffix != ".smt2"):
                    raise ValueError(f"unexpected archive member: {member.name}")
                data = tar.extractfile(member).read()
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open("xb") as stream:
                    stream.write(data)
                entries.append({"file": member.name, "bytes": len(data),
                                "sha256": hashlib.sha256(data).hexdigest(),
                                **query_metadata(data.decode())})
        if process.wait() != 0:
            raise ValueError("archive decompression failed")
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.kill()
        process.wait()
    entries.sort(key=lambda row: row["file"])
    if len(entries) != 1778 or any(not row["answers"] for row in entries):
        raise ValueError("unexpected archive inventory")
    write_json(manifest, {"archive_sha256": ARCHIVE_SHA256, "entries": entries})


def classify(stdout, code, timed_out, expected):
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    answers = [line for line in lines if line in {"sat", "unsat", "unknown"}]
    mismatches = [i for i, (a, b) in enumerate(zip(answers, expected))
                  if a in {"sat", "unsat"} and b in {"sat", "unsat"} and a != b]
    if mismatches:
        outcome = "wrong-answer"
    elif any(line not in {"sat", "unsat", "unknown"} for line in lines):
        outcome = "error"
    elif timed_out:
        outcome = "timeout"
    elif code != 0 or len(answers) != len(expected):
        outcome = "error"
    elif "unknown" in answers:
        outcome = "unknown"
    else:
        outcome = "complete"
    return {"outcome": outcome, "answers": answers, "mismatches": mismatches,
            "exit_code": code, "timed_out": timed_out, "queries": len(answers)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["inventory", "parser", "timing", "validate"])
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--timing-out", type=Path)
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--solver")
    parser.add_argument("--parser", default="build-slurm-rel/primo_parser_corpus")
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--expected-git")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--dolmen")
    parser.add_argument("--dolmen-timeout", type=float, default=60)
    parser.add_argument("--ref")
    parser.add_argument("--ref-arg", action="append", default=[])
    args = parser.parse_args()
    if args.mode == "inventory":
        inventory(args.archive, args.root, args.manifest)
        return 0
    if not args.solver or not args.out or not 0 <= args.shard_index < args.shards:
        parser.error("solver, out, and valid shard index required")
    args.out.mkdir(parents=True, exist_ok=False)
    version = subprocess.check_output([args.solver, "--version"], text=True, timeout=10)
    if args.expected_git and f"git: {args.expected_git}" not in version.splitlines():
        raise ValueError("candidate revision mismatch")
    binary_hash = digest(Path(args.solver))
    validation_tools = {}
    if args.mode == "validate":
        for label, tool in (("reference", args.ref), ("dolmen", args.dolmen)):
            validation_tools[label] = {
                "path": tool, "sha256": digest(Path(tool)),
                "version": subprocess.check_output([tool, "--version"], text=True, timeout=10)}
    write_json(args.out / "run.json", {
        "arguments": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
        "version": version, "solver_sha256": binary_hash,
        "driver_sha256": digest(Path(__file__)), "manifest_sha256": digest(args.manifest),
        "host": platform.node(), "slurm_job_id": os.environ.get("SLURM_JOB_ID"),
        "validation_tools": validation_tools,
    })
    entries = json.loads(args.manifest.read_text())["entries"]
    rows = []
    for index in range(args.shard_index, len(entries), args.shards):
        entry = entries[index]
        source = args.root / entry["file"]
        if digest(source) != entry["sha256"]:
            raise ValueError("input checksum mismatch")
        out = args.out / f"session-{index:04d}"
        if args.mode == "parser":
            out.mkdir()
            parsed = subprocess.run([args.parser, str(source)], capture_output=True,
                                    text=True, timeout=args.timeout, check=True)
            (out / "parser.stdout").write_text(parsed.stdout)
            (out / "parser.stderr").write_text(parsed.stderr)
            stats = json.loads(parsed.stdout)
            good = len(stats["query_ends"]) == entry["queries"] and stats["unsupported"] == 0
            row = {"outcome": "pass" if good else "error", "parser": stats}
        elif args.mode == "timing":
            out.mkdir()
            code, timed_out, seconds = run_session(
                [args.solver, *args.solver_arg], source, out, args.timeout)
            row = classify((out / "stdout").read_text(), code, timed_out, entry["answers"])
            row.update({"seconds": seconds, "par2": seconds if row["outcome"] == "complete"
                        else 2 * args.timeout})
            rss = (out / "maxrss-kib").read_text().splitlines()
            row["maxrss_kib"] = int(rss[-1]) if rss and rss[-1].isdigit() else None
        else:
            prior = json.loads((args.timing_out / f"session-{index:04d}" / "result.json").read_text())
            if prior["outcome"] != "complete":
                row = {"outcome": "unqualified-timing", "timing_outcome": prior["outcome"]}
            else:
                # Live independent reference checks avoid treating candidate
                # answers or incompletely annotated native metadata as an oracle.
                row = validate(source.read_text(), out, args)
        row.update({"index": index, "file": entry["file"]})
        rows.append(row)
        out.mkdir(exist_ok=True)
        write_json(out / "result.json", row)
        write_json(args.out / "summary.json", rows)
        if row["outcome"] in {"wrong-answer", "error", "failure"}:
            print(json.dumps(row))
            return 1
    if digest(Path(args.solver)) != binary_hash:
        raise ValueError("binary changed during run")
    print(json.dumps({"sessions": len(rows), "outcomes": {
        name: sum(row["outcome"] == name for row in rows)
        for name in sorted({row["outcome"] for row in rows})}}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
