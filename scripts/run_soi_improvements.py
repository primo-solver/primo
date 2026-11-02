#!/usr/bin/env python3
"""Run isolated process timings, separate check profiles and SAT model replays.

Reuses Primo's established process protocol, deterministic input/configuration
schedule, exact-input hashes and native+Dolmen replay machinery. Timing never
turns on phase clocks. Profiles are a separately identified measurement.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import run_lra_simplex_ablation as base
from analyze_lra_call_profile import analyze
from bench_qf_uf_full import run_solver
from benchmark_model_replay import digest, write_json


def measure(args, plan, names):
    out = args.out / args.phase / f"shard-{args.shard}"
    out.mkdir(parents=True, exist_ok=False)
    meta = base.metadata(args, plan)
    meta["workflow_sha256"][Path(__file__).name] = digest(Path(__file__))
    if meta["solver"]["sha256"] != plan["solver_sha256"]:
        raise ValueError("binary differs from frozen plan")
    write_json(out / "metadata.json", meta)
    expected = json.loads((args.corpus / "expected-statuses.json").read_text())
    written = 0
    with (out / "rows.jsonl").open("w") as stream:
        for name, repeat, config in base.schedule(plan, names, args.shard, args.shards):
            key = f"{Path(name).stem}-r{repeat}-{config['id']}"
            case = out / key
            case.mkdir()
            path = args.corpus / name
            checksum = digest(path)
            if checksum != path.stem:
                raise ValueError("frozen input content differs from filename")
            command = [str(args.solver), *plan["common_args"], *config["args"]]
            profile = case / "calls.jsonl"
            if args.phase == "profile":
                command.extend(["--lra-call-profile", str(profile)])
            run = run_solver(command, path, plan["timeout_seconds"])
            for label in ("stdout", "stderr"):
                (case / label).write_text(run.pop(label))
            analysis = (
                analyze(profile, run["status"])
                if args.phase == "profile"
                else {"coverage": "uninstrumented"}
            )
            if args.phase == "profile" and run["status"] == "ok":
                if analysis["coverage"] not in ("complete", "no-checks"):
                    raise ValueError("successful process has an incomplete profile")
                if analysis["calls"] != int(
                    run["stats"].get(":lra.online-check-calls", 0)
                ):
                    raise ValueError("profile/statistics check counts disagree")
                if analysis["total_pivots"] != int(run["stats"].get(":lra.pivots", 0)):
                    raise ValueError("profile/statistics pivots disagree")
            if digest(path) != checksum:
                raise ValueError("input changed during measurement")
            row = {
                "key": key,
                "file": name,
                "input_sha256": checksum,
                "repeat": repeat,
                "configuration": config["id"],
                "expected": expected[name],
                "command": [*command, str(path)],
                "run": run,
                "analysis": analysis,
                "trace_sha256": digest(profile) if profile.exists() else None,
            }
            stream.write(json.dumps(row) + "\n")
            stream.flush()
            written += 1
            base.check_run(run, expected[name])
    if digest(args.solver) != meta["solver"]["sha256"]:
        raise ValueError("binary changed during measurement")
    meta.update(complete=True, finished=base.now(), rows=written)
    write_json(out / "metadata.json", meta)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("timing", "profile", "models"))
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--dolmen", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--shards", type=int, default=64)
    parser.add_argument("--shard", type=int, required=True)
    args = parser.parse_args()
    for key in ("plan", "corpus", "solver", "out"):
        setattr(args, key, getattr(args, key).resolve())
    if args.dolmen:
        args.dolmen = args.dolmen.resolve()
    if not 0 <= args.shard < args.shards:
        parser.error("invalid shard index/count")
    plan = json.loads(args.plan.read_text())
    for filename, field in (
        ("manifest.txt", "manifest_sha256"),
        ("expected-statuses.json", "expected_statuses_sha256"),
    ):
        if digest(args.corpus / filename) != plan[field]:
            parser.error("corpus identity mismatch")
    manifest = (args.corpus / "manifest.txt").read_text().splitlines()
    names = plan.get("selected_files", manifest)
    if (
        len(names) != len(set(names))
        or len(names) != plan["inputs"]
        or not set(names).issubset(manifest)
    ):
        parser.error("invalid selected inputs")
    if args.phase == "models":
        if not args.dolmen:
            parser.error("models requires --dolmen")
        base.models(args, plan)
    else:
        measure(args, plan, names)


if __name__ == "__main__":
    main()
