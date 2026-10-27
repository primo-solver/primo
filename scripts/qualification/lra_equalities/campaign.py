#!/usr/bin/env python3
"""Run four default-off equality portfolio arms with the shared ablation tools."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

PROFILES = (
    ("default", ["--quiet"]),
    ("equality", ["--quiet", "--lra-equality-propagation"]),
    ("both", ["--quiet", "--lra-model-phase", "--monotone-elimination"]),
    ("both-equality", ["--quiet", "--lra-model-phase", "--monotone-elimination",
                       "--lra-equality-propagation"]),
)


def main() -> int:
    # Reuse the maintained status-aware answer gate and separate model replay.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lra_ablation"))
    import run as ablation

    ablation.PROFILES = PROFILES
    if sys.argv[1] != "mutation":
        return ablation.main()
    if not os.environ.get("SLURM_JOB_ID"):
        raise ValueError("SLURM allocation required")
    shard, profile = divmod(int(os.environ["SLURM_ARRAY_TASK_ID"]), 4)
    if profile not in {1, 3} or shard not in {0, 1}:
        raise ValueError("mutation tasks must be 1,3,5,7")
    name, arguments = PROFILES[profile]
    out = Path(os.environ["PRIMO_CAMPAIGN_OUT"]) / name / f"shard-{shard}"
    out.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([ablation.SOLVER, "--version"], text=True)
    if f"git: {os.environ['PRIMO_EXPECTED_GIT']}" not in version.splitlines():
        raise ValueError("unexpected solver revision")
    seed = 2026102100 + shard
    seeds = "qf_lra_real" if shard == 0 else "qf_lra"
    command = [
        "scripts/yinyang_fuzz.sh", "--seeds", f"benchmarks/seeds/{seeds}",
        "--solver", " ".join([ablation.SOLVER, *arguments]), "--ref", ablation.REFERENCE,
        "--seconds", "180", "--iterations", "100", "--solver-timeout", "5",
        "--keep-logic", "--keep-mutants", "--random-seed", str(seed),
        "--typefuzz", str(Path("scripts/seeded_opfuzz.py").resolve()),
        "--config", "benchmarks/fuzz-configs/qf_lra_equality_portfolio.txt",
        "--bugs", str(out / "bugs"), "--scratch", str(out / "scratch"),
        "--logs", str(out / "logs"),
    ]
    ablation.write(out / "execution-mutation.json", {
        "git": os.environ["PRIMO_EXPECTED_GIT"], "job": os.environ["SLURM_JOB_ID"],
        "binary_sha256": ablation.digest(Path(ablation.SOLVER)), "command": command,
        "seed": seed, "solver_args": arguments,
    })
    ablation.execute(command)
    if any((out / "bugs").rglob("*.smt2")):
        raise ValueError("mutation failures require triage")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
