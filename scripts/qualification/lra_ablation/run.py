#!/usr/bin/env python3
"""Execute default-off QF_LRA ablations using the shared benchmark/model tools."""

from __future__ import annotations

import csv
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

PROFILES = (
    ("default", ["--quiet"]),
    ("phase", ["--quiet", "--lra-model-phase"]),
    ("monotone", ["--quiet", "--monotone-elimination"]),
    ("both", ["--quiet", "--lra-model-phase", "--monotone-elimination"]),
)
SOLVER = "build-slurm-rel/primo"
REFERENCE = "contrib/local/bin/z3"
DOLMEN = "contrib/local/bin/dolmen"


def execute(command: list[str]) -> None:
    subprocess.run(command, check=True)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def rows(path: Path) -> list[dict[str, str]]:
    with path.open() as stream:
        return list(csv.DictReader(stream))


def solver_args(arguments: list[str]) -> list[str]:
    return [f"--solver-arg={argument}" for argument in arguments]


def gate(out: Path, shard: int, arguments: list[str], seed: int) -> None:
    if shard >= 6:
        execute([
            "python3", "scripts/qf_lra_boolean_arithmetic_fuzz.py", "--solver", SOLVER,
            *solver_args(arguments), "--z3", REFERENCE, "--dolmen", DOLMEN,
            "--count", "500", "--seed", str(seed), "--timeout", "5",
            "--dolmen-timeout", "30", "--out", str(out / "boolean-arithmetic"),
        ])
        return
    answers = out / "answers"
    execute([
        "python3", "scripts/fuzz_external.py", "--fuzzer", "fuzzsmt",
        "--solver", " ".join([SOLVER, *arguments]), "--refs", "z3", "--logic", "QF_LRA",
        "--count", "500", "--timeout", "5", "--seed", str(seed),
        "--fuzzer-args", "-ms 1 -Ms 3 -mv 1 -Mv 3 -mf 1 -mp 1 -ma 1 -Ma 2",
        "--out", str(answers),
    ])
    checked = rows(answers / "summary.csv")
    if len(checked) != 500 or any(r["outcome"] not in {"PASS", "TIMEOUT"} for r in checked):
        raise ValueError("generated answer gate has failures or incomplete row coverage")
    answer_check = out / "status-aware-answers.csv"
    status = subprocess.run([
        "python3", "scripts/check_solver_seed_corpus.py", "--seeds", str(answers / "generated"),
        "--solver", SOLVER, *solver_args(arguments), "--ref", REFERENCE,
        "--timeout", "5", "--out", str(answer_check),
    ], check=False).returncode
    checked = rows(answer_check)
    if status not in {0, 1} or len(checked) != 500:
        raise ValueError("status-aware generated answer replay is incomplete")
    for row in checked:
        timeout_only = (row["outcome"] == "INCOMPLETE" and
                        "timeout" in (row["ours"], row["reference"]) and
                        row["ours"] in {"sat", "unsat", "timeout"} and
                        row["reference"] in {"sat", "unsat", "timeout"} and
                        row["ours_exit"] in {"0", "124"} and row["reference_exit"] in {"0", "124"})
        if row["outcome"] != "PASS" and not timeout_only:
            raise ValueError(f"status-aware answer replay needs triage: {row}")
    sat = [str(Path(r["file"]).resolve()) for r in checked
           if r["outcome"] == "PASS" and r["ours"] == "sat" and r["ours_exit"] == "0"]
    manifest = out / "sat-inputs.txt"
    manifest.write_text("".join(name + "\n" for name in sat))
    if sat:
        execute([
            "python3", "scripts/validate_model.py", "--solver", SOLVER,
            *solver_args([*arguments, "--check-model"]), "--expect-sat",
            "--require-dolmen-pass", "--dolmen", DOLMEN, "--dolmen-arg=--strict=false",
            "--dolmen-arg=--force-smtlib2-logic=QF_NRA", "--timeout", "30",
            "--dolmen-timeout", "30", "--manifest", str(manifest), "--out", str(out / "models"),
        ])
        model_rows = rows(out / "models/summary.csv")
        if {r["file"] for r in model_rows} != set(sat) or len(model_rows) != len(sat):
            raise ValueError("generated SAT-model coverage is incomplete or duplicated")
        if any(r["outcome"] != "pass" or r["validator"] != "dolmen" for r in model_rows):
            raise ValueError("generated SAT models lack direct Dolmen acceptance")
    write(out / "gate-complete.json", {
        "rows": len(checked), "timeouts": sum(r["outcome"] == "INCOMPLETE" for r in checked),
        "sat_models": len(sat), "validator_logic": "QF_NRA semantic superset",
        "answer_summary_sha256": digest(answers / "summary.csv"),
        "status_aware_summary_sha256": digest(answer_check),
    })


def main() -> int:
    if not os.environ.get("SLURM_JOB_ID"):
        raise ValueError("SLURM allocation required")
    phase = sys.argv[1]
    task = int(os.environ.get("SLURM_ARRAY_TASK_ID", "0"))
    shard, profile = divmod(task, 4)
    name, arguments = PROFILES[profile]
    expected = os.environ["PRIMO_EXPECTED_GIT"]
    version = subprocess.check_output([SOLVER, "--version"], text=True)
    if f"git: {expected}" not in version.splitlines():
        raise ValueError("unexpected solver revision")
    if "--no-lra-model-phase" in subprocess.check_output([SOLVER, "--help"], text=True):
        raise ValueError("this driver requires the default-off implementation")
    base = Path(os.environ["PRIMO_CAMPAIGN_OUT"])
    out = base / name / f"shard-{shard}"
    out.mkdir(parents=True, exist_ok=True)
    seed = int(os.environ.get("PRIMO_SEED_BASE", "2026101600")) + shard
    models = sorted({line.split(":", 1)[1].strip()
                     for line in Path("/proc/cpuinfo").read_text().splitlines()
                     if line.startswith("model name")})
    if len(models) != 1 or not models[0].startswith("Intel(R) Xeon(R) CPU E5-2690 v4"):
        raise ValueError(f"unexpected comparison CPU: {models}")
    write(out / f"execution-{phase}.json", {
        "version": version, "binary_sha256": digest(Path(SOLVER)),
        "solver_args": arguments, "phase": phase, "seed": seed,
        "profile": name, "shard": shard, "task": task,
        "job": os.environ["SLURM_JOB_ID"], "cpu_models": models,
        "affinity": sorted(os.sched_getaffinity(0)),
        "memory_mib": os.environ.get("SLURM_MEM_PER_NODE"),
        "partition": os.environ.get("SLURM_JOB_PARTITION"),
        "driver_sha256": digest(Path(__file__)),
        "reference_sha256": digest(Path(REFERENCE)), "validator_sha256": digest(Path(DOLMEN)),
    })
    if phase == "gate":
        if shard >= 8:
            raise ValueError("gate shard outside 0..7")
        gate(out, shard, arguments, seed)
    elif phase == "precheck":
        execute(["ctest", "--test-dir", "build-slurm-rel", "--timeout", "120",
                 "--output-on-failure", "-j4"])
        for profile_name, profile_args in PROFILES:
            for seeds in ("qf_lra", "qf_lra_real"):
                execute([
                    "python3", "scripts/check_solver_seed_corpus.py", "--seeds",
                    f"benchmarks/seeds/{seeds}", "--solver", SOLVER, *solver_args(profile_args),
                    "--ref", REFERENCE, "--timeout", "30", "--out",
                    str(base / f"precheck-{profile_name}-{seeds}.csv"),
                ])
    elif phase == "mutation":
        if profile != 3 or shard >= 3:
            raise ValueError("mutation tasks must be 3, 7, or 11")
        command = [
            "scripts/yinyang_fuzz.sh", "--seeds", "benchmarks/seeds/qf_lra_division",
            "--solver", " ".join([SOLVER, *arguments]), "--ref", REFERENCE,
            "--seconds", "600", "--iterations", "100", "--solver-timeout", "5",
            "--file-size-limit", "200000", "--keep-logic", "--keep-mutants",
            "--random-seed", str(seed), "--bugs", str(out / "bugs"),
            "--scratch", str(out / "scratch"), "--logs", str(out / "logs"),
        ]
        if shard == 0:
            command += ["--typefuzz", str(Path("scripts/seeded_opfuzz.py").resolve()),
                        "--config", "benchmarks/fuzz-configs/qf_lra_operator_mutations.txt"]
        else:
            command += ["--hoist-declarations", "--config",
                        "benchmarks/fuzz-configs/qf_lra_typefuzz.txt"]
        execute(command)
        if any((out / "bugs").rglob("*.smt2")):
            raise ValueError("mutation failures require independent replay and triage")
    elif phase == "mutants":
        if shard >= 8:
            raise ValueError("retained-mutant replay shard outside 0..7")
        source = Path(os.environ["PRIMO_MUTANT_ROOT"])
        files = sorted(source.rglob("*.smt2"))
        if len(files) < 8:
            raise ValueError("insufficient retained mutation inputs")
        inputs = out / "inputs"
        inputs.mkdir(exist_ok=False)
        identities = {}
        for index, path in enumerate(files[shard::8]):
            (inputs / f"{index}.smt2").symlink_to(path.resolve())
            identities[str(path)] = digest(path)
        write(out / "input-identities.json", identities)
        execute([
            "python3", "scripts/check_solver_seed_corpus.py", "--seeds", str(inputs),
            "--ignore-declared-status", "--solver", SOLVER, *solver_args(arguments),
            "--ref", REFERENCE, "--timeout", "5", "--out", str(out / "answers.csv"),
        ])
    elif phase in {"benchmark", "replay"}:
        shards = int(os.environ["PRIMO_SHARDS"])
        if shard >= shards:
            raise ValueError("benchmark shard outside configured range")
        command = ["python3", "scripts/benchmark_model_replay.py", phase, "--out", str(out)]
        if phase == "benchmark":
            manifest = Path(os.environ["PRIMO_MANIFEST"])
            names = [s.strip() for s in manifest.read_text().splitlines() if s.strip()]
            if len(names) != int(os.environ["PRIMO_EXPECTED_INPUTS"]) or len(names) != len(set(names)):
                raise ValueError("manifest count or uniqueness mismatch")
            if digest(manifest) != os.environ["PRIMO_MANIFEST_SHA256"]:
                raise ValueError("manifest checksum mismatch")
            command += ["--root", os.environ["PRIMO_CORPUS_ROOT"], "--manifest", str(manifest),
                        "--solver", SOLVER, "--expected-git", expected,
                        "--run-id", os.environ["PRIMO_CAMPAIGN_ID"] + "-" + name,
                        "--shards", str(shards), "--shard-index", str(shard),
                        "--timeout", os.environ["PRIMO_TIMEOUT"], *solver_args(arguments)]
        else:
            command += ["--model-timeout", os.environ["PRIMO_MODEL_TIMEOUT"],
                        "--dolmen-timeout", "120", "--dolmen", DOLMEN,
                        "--dolmen-size", "5G", "--dolmen-arg=--strict=false"]
        execute(command)
    else:
        raise ValueError(f"unsupported phase: {phase}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
