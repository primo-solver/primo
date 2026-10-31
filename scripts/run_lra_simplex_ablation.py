#!/usr/bin/env python3
"""Run the frozen factorial experiment in docs/papers/lra-call-ablation.md.

This is experiment bookkeeping, not a new simplex algorithm. The existing
runner owns process timeout/exit semantics; the profiler owns completed-check
measurements. A timeout's durable prefix is never treated as its total work.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from analyze_lra_call_profile import analyze
from bench_qf_uf_full import run_solver
from benchmark_model_replay import digest, write_json
from validate_qfuf_models_with_dolmen import model_problem_text


def now():
    return datetime.now(timezone.utc).isoformat()


def rank(seed, *parts):
    return hashlib.sha256(json.dumps([seed, *parts]).encode()).hexdigest()


def schedule(plan, names, shard, shards):
    """Keep all configurations/repeats of an input on one allocated CPU."""
    ordered = sorted(names, key=lambda name: rank(plan["seed"], "input", name))
    for position, name in enumerate(ordered):
        if position % shards != shard:
            continue
        for repeat in range(plan["repeats"]):
            configs = sorted(
                plan["configurations"],
                key=lambda cfg: rank(plan["seed"], name, repeat, cfg["id"]),
            )
            for config in configs:
                yield name, repeat, config


def decisive(run):
    return (
        run["answer"]
        if run["status"] == "ok"
        and int(run["exit_code"]) == 0
        and run["timeout"] == "false"
        and run["answer"] in ("sat", "unsat")
        else None
    )


def check_run(run, expected):
    if run["status"] in ("error", "unsupported"):
        raise ValueError("solver error/unsupported requires triage")
    answer = decisive(run)
    if answer and expected != "unknown" and answer != expected:
        raise ValueError("wrong answer requires triage")


def identity(path):
    proc = subprocess.run(
        [str(path), "--version"], capture_output=True, text=True, timeout=15, check=True
    )
    return {"sha256": digest(path), "version": proc.stdout + proc.stderr}


def metadata(args, plan):
    return {
        "schema": 1,
        "started": now(),
        "phase": args.phase,
        "complete": False,
        "plan_sha256": digest(args.plan),
        "manifest_sha256": digest(args.corpus / "manifest.txt"),
        "expected_statuses_sha256": digest(args.corpus / "expected-statuses.json"),
        "solver": identity(args.solver),
        "workflow_sha256": {
            p.name: digest(p)
            for p in (
                Path(__file__),
                Path(__file__).with_name("analyze_lra_call_profile.py"),
                Path(__file__).with_name("bench_qf_uf_full.py"),
                Path(__file__).with_name("validate_qfuf_models_with_dolmen.py"),
            )
        },
        "host": platform.node(),
        "platform": platform.platform(),
        "cpu_model": next(
            (
                line.split(":", 1)[1].strip()
                for line in Path("/proc/cpuinfo").read_text().splitlines()
                if line.startswith("model name")
            ),
            "unknown",
        ),
        "affinity": sorted(os.sched_getaffinity(0)),
        "job_id": os.environ.get("SLURM_JOB_ID"),
        "array_job_id": os.environ.get("SLURM_ARRAY_JOB_ID"),
        "shard": args.shard,
        "shards": args.shards,
        "seed": plan["seed"],
        "timeout_seconds": plan["timeout_seconds"],
    }


def timing(args, plan, names):
    out = args.out / "timing" / f"shard-{args.shard}"
    out.mkdir(parents=True, exist_ok=False)
    meta = metadata(args, plan)
    if meta["solver"]["sha256"] != plan["solver_sha256"]:
        raise ValueError("solver identity differs from frozen plan")
    write_json(out / "metadata.json", meta)
    expected = json.loads((args.corpus / "expected-statuses.json").read_text())
    written = 0
    with (out / "rows.jsonl").open("w") as stream:
        for name, repeat, config in schedule(plan, names, args.shard, args.shards):
            key = f"{Path(name).stem}-r{repeat}-{config['id']}"
            case = out / key
            case.mkdir()
            path = args.corpus / name
            checksum = digest(path)
            if checksum != path.stem:
                raise ValueError("input content does not match frozen filename")
            profile = case / "calls.jsonl"
            command = [
                str(args.solver),
                *plan["common_args"],
                *config["args"],
                "--lra-call-profile",
                str(profile),
            ]
            run = run_solver(command, path, plan["timeout_seconds"])
            for label in ("stdout", "stderr"):
                (case / label).write_text(run.pop(label))
            analysis = analyze(profile, run["status"])
            if run["status"] == "ok" and analysis["coverage"] not in (
                "complete",
                "no-checks",
            ):
                raise ValueError("successful process has incomplete profiling")
            if analysis["coverage"] in ("complete", "no-checks"):
                if analysis["calls"] != int(
                    run["stats"].get(":lra.online-check-calls", 0)
                ):
                    raise ValueError("profile/statistics check counts disagree")
                if analysis["total_pivots"] != int(run["stats"].get(":lra.pivots", 0)):
                    raise ValueError("profile/statistics pivot counts disagree")
            if digest(path) != checksum:
                raise ValueError("input changed during timing")
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
            check_run(run, expected[name])
            if written % 48 == 0:
                print(f"{written} timing rows complete", flush=True)
    if digest(args.solver) != meta["solver"]["sha256"]:
        raise ValueError("binary changed during timing")
    meta.update(complete=True, finished=now(), rows=written)
    write_json(out / "metadata.json", meta)


def read_rows(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def model_groups(rows):
    groups = {}
    for row in rows:
        if decisive(row["run"]) == "sat":
            groups.setdefault((row["configuration"], row["repeat"]), []).append(row)
    return groups


def models(args, plan):
    parent = args.out / "timing" / f"shard-{args.shard}"
    before = json.loads((parent / "metadata.json").read_text())
    if not before["complete"]:
        raise ValueError("timing shard incomplete")
    out = args.out / "models" / f"shard-{args.shard}"
    out.mkdir(parents=True, exist_ok=False)
    meta = metadata(args, plan)
    meta["dolmen"] = identity(args.dolmen)
    for key in ("solver", "plan_sha256", "manifest_sha256", "expected_statuses_sha256"):
        if meta[key] != before[key]:
            raise ValueError(f"timing/model identity mismatch: {key}")
    write_json(out / "metadata.json", meta)
    configs = {cfg["id"]: cfg for cfg in plan["configurations"]}
    passed = 0
    joins = []
    for (config_id, repeat), rows in model_groups(
        read_rows(parent / "rows.jsonl")
    ).items():
        target = out / f"{config_id}-r{repeat}"
        manifest = out / f"{config_id}-r{repeat}.txt"
        manifest.write_text("".join(str(args.corpus / r["file"]) + "\n" for r in rows))
        solver_args = [
            *plan["common_args"],
            *configs[config_id]["args"],
            "--check-model",
        ]
        command = [
            sys.executable,
            str(Path(__file__).with_name("validate_model.py")),
            "--solver",
            str(args.solver),
            "--manifest",
            str(manifest),
            "--out",
            str(target),
            "--expect-sat",
            "--require-dolmen-pass",
            "--dolmen",
            str(args.dolmen),
            "--dolmen-size",
            "2G",
            "--dolmen-arg=--strict=false",
            "--timeout",
            str(plan["model_timeout_seconds"]),
            "--dolmen-timeout",
            "30",
            *[f"--solver-arg={arg}" for arg in solver_args],
        ]
        with (out / f"{config_id}-r{repeat}.log").open("w") as log:
            proc = subprocess.run(
                command, stdout=log, stderr=subprocess.STDOUT, check=False
            )
        with (target / "summary.csv").open() as stream:
            checks = list(csv.DictReader(stream))
        if len(checks) != len(rows):
            raise ValueError("missing model rows")
        for index, (row, check) in enumerate(zip(rows, checks, strict=True)):
            path = args.corpus / row["file"]
            if Path(check["file"]).resolve() != path.resolve():
                raise ValueError("model/input join mismatch")
            adapted = target / f"{index:05d}-{path.name}.smt2"
            if digest(path) != row[
                "input_sha256"
            ] or adapted.read_text() != model_problem_text(path.read_text()):
                raise ValueError("model assertions changed")
            if (
                check["answer"],
                check["outcome"],
                check["validator"],
                check["solver_exit"],
                check["dolmen_exit"],
            ) != ("sat", "pass", "dolmen", "0", "0"):
                raise ValueError(
                    f"model validation failed/incomplete: {row['key']}: {check}"
                )
            joins.append(
                {
                    "key": row["key"],
                    "adapted_sha256": digest(adapted),
                    "outcome": "pass",
                    "validator": "dolmen",
                }
            )
            passed += 1
        if proc.returncode:
            raise ValueError("validator process failed")
        write_json(out / "joins.json", joins)
        print(f"{passed} SAT timing rows independently validated", flush=True)
    if (
        digest(args.solver) != meta["solver"]["sha256"]
        or digest(args.dolmen) != meta["dolmen"]["sha256"]
    ):
        raise ValueError("tool changed during model checks")
    meta.update(complete=True, finished=now(), passed=passed)
    write_json(out / "metadata.json", meta)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("timing", "models"))
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
    for filename, field in [
        ("manifest.txt", "manifest_sha256"),
        ("expected-statuses.json", "expected_statuses_sha256"),
    ]:
        if digest(args.corpus / filename) != plan[field]:
            parser.error("corpus identity mismatch")
    names = (args.corpus / "manifest.txt").read_text().splitlines()
    if len(names) != len(set(names)) or len(names) != plan["inputs"]:
        parser.error("invalid frozen manifest")
    if args.phase == "timing":
        timing(args, plan, names)
    else:
        if args.dolmen is None:
            parser.error("models requires --dolmen")
        models(args, plan)


if __name__ == "__main__":
    main()
