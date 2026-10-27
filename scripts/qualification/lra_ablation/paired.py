#!/usr/bin/env python3
"""Repeat each lost solve in three pairs on one physical CPU allocation."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

from run import PROFILES, SOLVER, digest, rows, solver_args, write


def main() -> int:
    phase = sys.argv[1]
    task = int(os.environ["SLURM_ARRAY_TASK_ID"])
    plan = json.loads(Path(os.environ["PRIMO_PAIR_PLAN"]).read_text())
    selected = plan["inputs"][task]
    source = Path("tmp/QF_LRA") / selected["file"]
    if digest(source) != selected["sha256"] or digest(Path(SOLVER)) != plan["binary_sha256"]:
        raise ValueError("paired timing input or solver identity changed")
    models = sorted({line.split(":", 1)[1].strip()
                     for line in Path("/proc/cpuinfo").read_text().splitlines()
                     if line.startswith("model name")})
    if (models != ["Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz"]
            or os.environ["SLURM_JOB_PARTITION"] != "compute"
            or os.environ["SLURM_MEM_PER_NODE"] != "7168"):
        raise ValueError("paired allocation differs from the original timings")
    base = Path(os.environ["PRIMO_PAIR_OUT"]) / f"input-{task}"
    base.mkdir(parents=True, exist_ok=True)
    write(base / f"execution-{phase}.json", {
        "job": os.environ["SLURM_JOB_ID"], "array_task": task,
        "cpu_models": models, "affinity": sorted(os.sched_getaffinity(0)),
        "plan_sha256": digest(Path(os.environ["PRIMO_PAIR_PLAN"])),
        "binary_sha256": plan["binary_sha256"], "input": selected,
        "memory_mib": os.environ["SLURM_MEM_PER_NODE"], "phase": phase,
    })
    manifest = base / "input.txt"
    manifest.write_text(selected["file"] + "\n")
    options = dict(PROFILES)
    for repetition in range(3):
        order = ["default", plan["profile"]]
        if (task + repetition) % 2:
            order.reverse()
        for name in order:
            out = base / f"round-{repetition}" / name
            command = ["python3", "scripts/benchmark_model_replay.py", phase, "--out", str(out)]
            if phase == "benchmark":
                command += ["--root", "tmp/QF_LRA", "--manifest", str(manifest),
                            "--solver", SOLVER, "--expected-git", "4200525",
                            "--run-id", f"lra-loss-{plan['dataset']}-{name}-{task}-{repetition}",
                            "--shards", "1", "--shard-index", "0",
                            "--timeout", str(plan["timeout"]), *solver_args(options[name])]
            elif phase == "replay":
                command += ["--model-timeout", "3600" if plan["timeout"] == 1200 else "120",
                            "--dolmen-timeout", "120", "--dolmen", "contrib/local/bin/dolmen",
                            "--dolmen-size", "5G", "--dolmen-arg=--strict=false"]
            else:
                raise ValueError("unknown paired phase")
            subprocess.run(command, check=True)
            if phase == "benchmark":
                observed = rows(out / "answers.csv")
                if (len(observed) != 1 or observed[0]["wrong"] != "false"
                        or observed[0]["status"] not in {"ok", "timeout"}
                        or (observed[0]["status"] == "ok"
                            and (observed[0]["exit_code"] != "0"
                                 or observed[0]["answer"] not in {"sat", "unsat", "unknown"}))):
                    subprocess.run(["scancel", os.environ["SLURM_ARRAY_JOB_ID"]], check=False)
                    raise ValueError("paired correctness error; cancelled this array")
        if phase == "benchmark":
            a, b = [rows(base / f"round-{repetition}" / name / "answers.csv")[0] for name in order]
            if (a["status"] == b["status"] == "ok" and a["answer"] in {"sat", "unsat"}
                    and b["answer"] in {"sat", "unsat"} and a["answer"] != b["answer"]):
                raise ValueError("paired decisive answer disagreement")
    write(base / f"{phase}-complete.json", {"repetitions": 3, "input": selected})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
