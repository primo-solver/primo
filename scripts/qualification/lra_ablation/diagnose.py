#!/usr/bin/env python3
"""Collect bounded counters and separate stack samples on the frozen LRA binary.

These are diagnostics, not replacement benchmark timings or model qualification.
"""

from __future__ import annotations

import json
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

from run import PROFILES, SOLVER, digest, write


def main() -> int:
    mode = sys.argv[1]
    task = int(os.environ["SLURM_ARRAY_TASK_ID"])
    plan_path = Path(os.environ["PRIMO_DIAG_PLAN"])
    plan = json.loads(plan_path.read_text())
    if mode == "counters":
        selected = plan["inputs"][task]
        profiles = list(PROFILES)
        # Rotate order to avoid always measuring one profile first.
        profiles = profiles[task % 4:] + profiles[:task % 4]
    elif mode == "profile":
        index, profile = divmod(task, 4)
        selected = plan["inputs"][plan["profile_inputs"][index]]
        profiles = [PROFILES[profile]]
    elif mode == "sensitivity":
        case = plan["sensitivity"][task]
        selected = plan["inputs"][case["input"]]
        arguments = dict(PROFILES)[case["profile"]]
        profiles = [(f"{case['profile']}-{case['rule']}",
                     [*arguments, f"--lra-pivoting-rule={case['rule']}"])]
    else:
        raise ValueError("unknown diagnostic mode")
    source = Path("tmp/QF_LRA") / selected["file"]
    models = sorted({line.split(":", 1)[1].strip()
                     for line in Path("/proc/cpuinfo").read_text().splitlines()
                     if line.startswith("model name")})
    if (digest(source) != selected["sha256"]
            or digest(Path(SOLVER)) != plan["binary_sha256"]
            or models != ["Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz"]
            or os.environ["SLURM_JOB_PARTITION"] != "compute"
            or os.environ["SLURM_MEM_PER_NODE"] != "7168"):
        raise ValueError("diagnostic input, binary, or allocation identity changed")
    base = Path(os.environ["PRIMO_DIAG_OUT"]) / mode / f"input-{task}"
    base.mkdir(parents=True, exist_ok=False)
    write(base / "execution.json", {
        "job": os.environ["SLURM_JOB_ID"], "array_task": task,
        "host": os.uname().nodename, "cpu_models": models,
        "affinity": sorted(os.sched_getaffinity(0)),
        "memory_mib": os.environ["SLURM_MEM_PER_NODE"],
        "plan_sha256": digest(plan_path), "input": selected,
        "binary_sha256": plan["binary_sha256"],
        "version": subprocess.check_output([SOLVER, "--version"], text=True),
        "mode": mode,
    })
    for name, arguments in profiles:
        command = [str(Path(SOLVER).resolve()), *arguments, "--stats",
                   "--cancel-grace-seconds", "8", str(source)]
        if mode == "profile":
            command = ["python3", str(Path(os.environ["PRIMO_WORKFLOW"]) /
                                       "profile_gdb_samples.py"),
                       "--out", str(base / name), "--timeout", "60",
                       "--interval", "0.2", "--seed", "1", "--", *command]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            (base / f"{name}.stdout").write_text(result.stdout)
            (base / f"{name}.stderr").write_text(result.stderr)
            if result.returncode not in {0, 1} or not (base / name / "summary.json").is_file():
                raise ValueError("profiler did not produce a complete diagnostic artifact")
            continue
        out = base / name
        out.mkdir()
        start = time.monotonic()
        timed_out = False
        killed = False
        with (out / "stdout").open("w") as stdout, (out / "stderr").open("w") as stderr:
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                       start_new_session=True)
            try:
                process.wait(timeout=plan["counter_seconds"])
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    killed = True
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
        output = (out / "stdout").read_text()
        error = (out / "stderr").read_text()
        answers = re.findall(r"^(sat|unsat|unknown)$", output, re.MULTILINE)
        stats = {key: int(value) for key, value in
                 re.findall(r"^(:\S+) (\d+)$", error, re.MULTILINE)}
        write(out / "summary.json", {
            "command": command, "seconds": time.monotonic() - start,
            "timeout": timed_out, "hard_killed": killed, "exit_code": process.returncode,
            "answer": answers[-1] if answers else None, "statistics": stats,
            "stdout_sha256": digest(out / "stdout"), "stderr_sha256": digest(out / "stderr"),
            "note": "Stats and cooperative cancellation perturb timing; diagnostics only.",
        })
        if ((not timed_out and (process.returncode != 0 or not answers))
                or (answers and answers[-1] in {"sat", "unsat"}
                    and answers[-1] != selected["expected"])):
            raise ValueError("diagnostic error or decisive answer disagreement")
    write(base / "complete.json", {"profiles": [name for name, _ in profiles]})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
