#!/usr/bin/env python3
"""Run one reproducible shared-assignment qualification shard inside SLURM."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import shlex
import shutil
import socket
import subprocess
from collections import Counter
from pathlib import Path

from compare_with_z3 import run_solver
from fuzz_external import classify


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fuzz_summary(rows: list[dict[str, str]], code: int) -> dict[str, object]:
    counts = Counter(row["outcome"] for row in rows)
    allowed = {"PASS", "TIMEOUT", "UNKNOWN_INVOLVED", "REFERENCE_UNUSABLE"}
    hard = len(rows) != 1000 or bool(set(counts) - allowed) or code not in (0, 1)
    for row in rows:
        references = set(row["refs"].split())
        concrete = references & {"sat", "unsat"}
        # A second reference timing out must not hide an answer disagreement.
        hard |= len(concrete) > 1 or bool(references - {"sat", "unsat", "unknown", "TIMEOUT"})
        hard |= row["ours"] in {"sat", "unsat"} and bool(concrete - {row["ours"]})
    return {"exit_code": code, "counts": dict(counts), "hard_failure": hard}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", type=int, required=True)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--uf-seeds", type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("solver batches require a SLURM allocation")
    if not 0 <= args.task <= 17:
        parser.error("task must be between 0 and 17")
    out = args.out.resolve() / f"task-{args.task}"
    out.mkdir(parents=True, exist_ok=False)
    solver = str(Path("build-slurm-rel/primo").resolve())
    version = subprocess.check_output([solver, "--version"], text=True)
    if f"git: {args.expected_git}" not in version.splitlines():
        raise ValueError("candidate revision mismatch")
    os.environ["PATH"] = str(Path("contrib/local/bin").resolve()) + ":" + os.environ["PATH"]
    common = ["--quiet", "--preprocessing-mode=off"]
    seed = 202609250 + args.task
    metadata = {
        "version": version, "binary_sha256": digest(Path(solver)),
        "host": socket.gethostname(), "job_id": os.environ["SLURM_JOB_ID"],
        "array_task": args.task, "seed": seed, "commands": [],
        "driver_sha256": digest(Path(__file__)), "references": {},
    }
    for name in ("z3", "cvc5", "dolmen"):
        path = Path(shutil.which(name) or "")
        if not path.is_file():
            raise ValueError(f"missing required tool: {name}")
        metadata["references"][name] = {
            "path": str(path), "sha256": digest(path),
            "version": subprocess.check_output([str(path), "--version"], text=True),
        }

    def run(command: list[str], name: str) -> int:
        metadata["commands"].append(command)
        (out / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print(shlex.join(command), flush=True)
        with (out / (name + ".log")).open("w") as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=False)
        return result.returncode

    if args.task < 4:
        logic = ["QF_UF", "QF_UF", "QF_LRA", "QF_LIA"][args.task]
        flags = common.copy()
        if args.task == 0:
            flags += ["--euf-prop=online"]
        elif args.task == 1:
            flags += ["--euf-engine=lazy", "--euf-prop=off"]
        generator = ("-ms 1 -Ms 2 -mv 2 -Mv 5 -mf 3 -mp 3 -ma 1 -Ma 3"
                     if args.task < 2 else "-mv 1 -Mv 4 -mc 1 -Mc 3 -Mbw 4")
        code = run(["python3", "scripts/fuzz_external.py", "--fuzzer", "fuzzsmt",
                    "--solver", shlex.join([solver, *flags]), "--refs", "z3", "cvc5",
                    "--logic", logic, "--count", "1000", "--seed", str(seed),
                    "--timeout", "5", "--fuzzer-args", generator,
                    "--out", str(out / "answers")], "answers")
        rows = list(csv.DictReader((out / "answers/summary.csv").open()))
        # Reference timeouts remain explicit unresolved rows, never passes.
        summary = fuzz_summary(rows, code)
    elif args.task in (4, 5):
        flags = ["--quiet"] + (["--qf-uflra-online-euf", "--euf-prop=online"]
                          if args.task == 5 else [])
        code = run(["env", "PRIMO_FUZZ_COUNT=1000", "PRIMO_FINITE_DOMAIN_FUZZ_COUNT=56",
                    "PRIMO_FUZZ_SEED_BASE=202609250", "PRIMO_FUZZ_MODEL_VALIDATION=1",
                    "PRIMO_FUZZ_SOLVER_ARGS=" + shlex.join(flags),
                    "PRIMO_FUZZ_OUT_DIR=" + str(out / "mixed"),
                    "bash", "scripts/slurm_qf_uflra_fuzz_gate.sbatch"], "mixed")
        summary = {"exit_code": code, "hard_failure": code != 0}
    elif args.task in (6, 7):
        flags = common + (["--euf-prop=online"] if args.task == 6 else
                          ["--no-qf-uflia-online-components"])
        code = run(["python3", "scripts/qf_uflia_fuzz.py", "--solver", solver,
                    *["--solver-arg=" + flag for flag in flags], "--ref", "z3",
                    "--ref", "cvc5", "--count", "1000", "--seed", str(seed),
                    "--timeout", "5", "--shape", "mixed", "--out", str(out / "answers")],
                   "answers")
        summary = {"exit_code": code, "hard_failure": code != 0}
    elif args.task in (8, 9):
        logic = "uf" if args.task == 8 else "lra"
        flags = common + ["--incremental-mode=assumptions"]
        if logic == "uf":
            flags += ["--euf-prop=online"]
        code = run(["python3", f"scripts/incremental_{logic}_gate.py", "--kind", "generated",
                    "--seed", str(seed), "--count", "100", "--solver", solver,
                    *["--solver-arg=" + flag for flag in flags],
                    "--ref", "contrib/local/bin/z3", "--dolmen", "contrib/local/bin/dolmen",
                    "--expected-git", args.expected_git, "--timeout", "10",
                    "--dolmen-timeout", "10", "--out", str(out / "sessions")], "sessions")
        summary = {"exit_code": code, "hard_failure": code != 0}
    elif args.task == 17:
        code = run(["python3", "scripts/qf_lra_boolean_arithmetic_fuzz.py", "--solver", solver,
                    *["--solver-arg=" + flag for flag in common], "--count", "1000",
                    "--seed", str(seed), "--timeout", "5", "--dolmen-timeout", "5",
                    "--dolmen", "contrib/local/bin/dolmen", "--out", str(out / "boolean")],
                   "boolean")
        summary = {"exit_code": code, "hard_failure": code != 0}
    else:
        seeds = sorted(args.uf_seeds.glob("*.smt2"))[args.task - 10::7]
        if not seeds:
            raise ValueError("empty UF mutation seed shard")
        staged = out / "seeds"
        staged.mkdir()
        metadata["seed_inputs"] = {str(path): digest(path) for path in seeds}
        for path in seeds:
            shutil.copy2(path, staged / path.name)
        code = run(["scripts/yinyang_fuzz.sh", "--seeds", str(staged),
                    "--solver", shlex.join([solver, *common]), "--ref", str(Path("contrib/local/bin/z3").resolve()),
                    "--seconds", "1200", "--iterations", "100", "--solver-timeout", "2",
                    "--bugs", str(out / "bugs"), "--scratch", str(out / "scratch"),
                    "--logs", str(out / "logs"), "--file-size-limit", "20000",
                    "--typefuzz", ".venv-yinyang/bin/typefuzz", "--random-seed", str(seed),
                    "--keep-logic", "--hoist-declarations", "--keep-mutants"], "mutation")
        # Do not infer a pass from an empty bug directory or YinYang's ignore list.
        # Independently classify every retained input, including exit status.
        counts: Counter[str] = Counter()
        with (out / "classified.jsonl").open("w") as stream:
            for path in sorted((out / "scratch").glob("*.smt2")):
                candidate = run_solver([solver, *common, str(path)], 2)
                reference = run_solver(["contrib/local/bin/z3", str(path)], 2)
                outcome = classify(candidate[0], [reference[0]])
                counts[outcome] += 1
                row = {"file": str(path), "sha256": digest(path), "candidate": candidate,
                       "reference": reference, "outcome": outcome}
                stream.write(json.dumps(row) + "\n")
                stream.flush()
        hard = code != 0 or not counts or bool(set(counts) - {"PASS", "TIMEOUT", "UNKNOWN_INVOLVED"})
        summary = {"exit_code": code, "counts": dict(counts), "hard_failure": hard}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary), flush=True)
    return int(summary["hard_failure"])


if __name__ == "__main__":
    raise SystemExit(main())
