#!/usr/bin/env python3
"""Collect per-call pivots/time on a fixed sharded QF_LRA manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

from analyze_lra_call_profile import analyze
from bench_qf_uf_full import family_for, parse_smtlib_status, read_manifest, run_solver
from prepare_lra_call_replay import check_conjunction


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--index", type=int, default=0)
    parser.add_argument("--capture-call", type=int)
    parser.add_argument("--export-min-pivots", type=int)
    parser.add_argument("--export-limit", type=int, default=5)
    parser.add_argument("--solver-arg", action="append", default=[])
    args = parser.parse_args()
    if args.capture_call is not None and args.capture_call <= 0:
        parser.error("capture call must be positive")
    args.root = args.root.resolve()
    assert args.shards > 0 and 0 <= args.index < args.shards
    args.out.mkdir(parents=True, exist_ok=False)
    solver = str(args.solver.resolve())
    reference = str(args.reference.resolve())
    metadata = {
        "solver_sha256": digest(args.solver),
        "workflow_sha256": {
            p.name: digest(p)
            for p in [
                Path(__file__),
                Path(__file__).with_name("analyze_lra_call_profile.py"),
                Path(__file__).with_name("prepare_lra_call_replay.py"),
            ]
        },
        "reference_sha256": digest(args.reference),
        "version": subprocess.check_output([solver, "--version"], text=True),
        "manifest_sha256": digest(args.manifest),
        "timeout_seconds": args.timeout,
        "solver_args": args.solver_arg,
        "shards": args.shards,
        "index": args.index,
        "job_id": os.environ.get("SLURM_JOB_ID"),
        "array_job_id": os.environ.get("SLURM_ARRAY_JOB_ID"),
        "affinity": sorted(os.sched_getaffinity(0)),
        "capture_call": args.capture_call,
        "export_min_pivots": args.export_min_pivots,
        "export_limit": args.export_limit,
        "records": [],
        "complete": False,
    }
    summary = args.out / "summary.json"
    for index, path in enumerate(read_manifest(args.manifest, args.root)):
        if index % args.shards != args.index:
            continue
        case = args.out / f"case-{index:04d}"
        case.mkdir()
        profile = case / "calls.jsonl"
        command = [
            solver,
            "--quiet",
            "--stats",
            *args.solver_arg,
            "--lra-call-profile",
            str(profile.resolve()),
        ]
        if args.capture_call is not None:
            command += [
                "--lra-call-capture",
                str((case / "capture").resolve()),
                "--lra-call-capture-call",
                str(args.capture_call),
            ]
        if args.export_min_pivots is not None:
            command += [
                "--lra-call-export",
                str((case / "cubes").resolve()),
                "--lra-call-export-min-pivots",
                str(args.export_min_pivots),
                "--lra-call-export-limit",
                str(args.export_limit),
            ]
        before = digest(path)
        native = run_solver(command, path, args.timeout)
        ref = run_solver([reference], path, args.timeout)
        for label, run in [("native", native), ("reference", ref)]:
            (case / (label + ".stdout")).write_text(run.pop("stdout"))
            (case / (label + ".stderr")).write_text(run.pop("stderr"))
        expected = parse_smtlib_status(path)
        checked = [
            r["answer"]
            for r in [native, ref]
            if r["status"] == "ok" and r["answer"] in ("sat", "unsat")
        ]
        wrong = len(set(checked)) > 1 or (
            expected and any(answer != expected for answer in checked)
        )
        assert before == digest(path)
        analysis = analyze(profile, native["status"])
        if analysis["coverage"] == "complete":
            assert analysis["calls"] == int(
                native["stats"].get(":lra.online-check-calls", 0)
            )
            assert analysis["total_pivots"] == int(
                native["stats"].get(":lra.pivots", 0)
            )
        record = {
            "index": index,
            "file": str(path.relative_to(args.root)),
            "family": family_for(path, args.root),
            "input_sha256": before,
            "expected": expected,
            "native": native,
            "reference": ref,
            "wrong": bool(wrong),
            "command": [*command, str(path)],
            "trace_sha256": digest(profile) if profile.exists() else None,
            "analysis": analysis,
        }
        if args.capture_call is not None:
            snapshot = case / "capture/capture.smt2"
            event = analysis.get("captures", [])
            record["capture"] = {
                "saved": snapshot.exists(),
                "event": event[0] if event else None,
            }
            if snapshot.exists():
                record["capture"].update(
                    {
                        "sha256": digest(snapshot),
                        "syntax": check_conjunction(snapshot.read_text()),
                        "classification": "unannounced"
                        if not event
                        else "no-durable-completion"
                        if event[0]["completion"] is None
                        else "completed",
                    }
                )
                assert record["capture"]["syntax"]["expected"] == "unknown"
                if event:
                    assert event[0]["file"] == "capture.smt2"
        metadata["records"].append(record)
        summary.write_text(json.dumps(metadata, indent=2) + "\n")
        print(
            record["file"],
            native["status"],
            native["answer"],
            analysis["calls"],
            flush=True,
        )
        if wrong or any(r["status"] in ("error", "unsupported") for r in [native, ref]):
            raise SystemExit("answer/error triage required: " + record["file"])
    sat_inputs = [
        str(args.root / row["file"])
        for row in metadata["records"]
        if row["native"]["status"] == "ok" and row["native"]["answer"] == "sat"
    ]
    (args.out / "parents-sat.txt").write_text(
        "".join(path + "\n" for path in sat_inputs)
    )
    metadata["sat_replays_expected"] = len(sat_inputs)
    metadata["complete"] = True
    summary.write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
