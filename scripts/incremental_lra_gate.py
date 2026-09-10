#!/usr/bin/env python3
"""Scoped QF_LRA generated/native-mutation correctness gate, with actual-model checks."""

import argparse
import hashlib
import json
import os
import platform
import random
import re
import subprocess
import sys
from pathlib import Path

from validate_incremental import command_spans, validate, write_json


def numeral(value):
    return f"(- {-value})" if value < 0 else str(value)


def generated(seed):
    rng = random.Random(seed)
    commands = ["(set-logic QF_LRA)"]
    commands += [f"(declare-const x{i} Real)" for i in range(6)]
    commands += [f"(declare-const b{i} Bool)" for i in range(4)]
    witness = [rng.randrange(-8, 9) for _ in range(6)]
    commands += [
        "(define-fun offset ((v Real)) Real (+ x0 v))",
        "(define-fun d () Real (ite b0 (offset 1) (offset 2)))",
    ]
    for _ in range(8):
        indices = rng.sample(range(6), 3)
        coefficients = [rng.choice([-3, -2, -1, 1, 2, 3]) for _ in indices]
        terms = [f"(* {numeral(c)} x{i})" for i, c in zip(indices, coefficients)]
        rhs = sum(witness[i] * c for i, c in zip(indices, coefficients))
        atom = f"(<= (+ {' '.join(terms)}) {numeral(rhs + rng.randrange(3))})"
        guard = f"b{rng.randrange(4)}"
        # Both encodings imply the witnessed bound, and share Boolean subterms.
        commands.append(f"(assert (and (or {guard} {atom}) (=> {guard} {atom})))")
    commands += ["(check-sat)", "(check-sat)", "(push 2)"]
    commands += [
        "(declare-const local Real)",
        "(assert (= local d))",
        "(assert (> local x0))",
        "(check-sat)",
        "(push 1)",
        "(assert (<= local x0))",
        "(check-sat)",
        "(pop 2)",
        "(check-sat)",
        "(declare-const local Bool)",
        "(assert local)",
        "(assert (= d (ite b0 (+ x0 1) (+ x0 2))))",
        "(check-sat)",
        "(pop 1)",
        "(push 1)",
        "(assert (<= d x0))",
        "(check-sat)",
        "(pop 1)",
        "(check-sat)",
    ]
    # New atoms after both SAT and UNSAT, plus an assertion-dependent-elimination trap.
    commands += [
        f"(assert (>= x0 {numeral(witness[0])}))",
        "(check-sat)",
        "(push 1)",
        f"(assert (< x0 {numeral(witness[0])}))",
        "(check-sat)",
        "(pop 1)",
        "(check-sat)",
        "(exit)",
    ]
    return "\n".join(commands) + "\n"


def native_prefix(path):
    text = path.read_text()
    commands = []
    for start, end, head in command_spans(text):
        commands.append((text[start:end], head))
        if head[0] == "check-sat":
            return commands
    raise ValueError(f"native seed has no query: {path}")


def mutate_native(commands, seed):
    rng = random.Random(seed)
    result = []
    depth = 0
    numbers = re.compile(r"(?<=[ (])\d+(?:\.\d+)?(?=[ )])")
    for raw, head in commands:
        kind = head[0]
        if kind == "set-info":
            continue  # Mutations invalidate source status annotations.
        if kind == "push":
            depth += int(head[1])
        if kind == "pop":
            depth -= int(head[1])
        if kind == "assert":
            raw = numbers.sub(
                lambda m: str(rng.randrange(0, 12)) if rng.random() < 0.15 else m[0],
                raw,
            )
            if rng.random() < 0.5:
                raw = raw.replace("(<= ", "(< ")
        result.append(raw)
    result += [f"(pop {depth})", "(check-sat)"]
    result += [
        "(push 1)",
        "(declare-const mutation_local Real)",
        f"(assert (>= mutation_local {rng.randrange(5)}))",
        "(check-sat)",
        "(assert (< mutation_local 0))",
        "(check-sat)",
        "(pop 1)",
        "(check-sat)",
        "(exit)",
    ]
    return "\n".join(result) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=["generated", "native"], required=True)
    parser.add_argument("--native-seed", type=Path, action="append", default=[])
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--ref", required=True)
    parser.add_argument("--ref-arg", action="append", default=[])
    parser.add_argument("--dolmen", required=True)
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--dolmen-timeout", type=float, default=20)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.count <= 0 or (args.kind == "native" and not args.native_seed):
        parser.error("positive count and native seeds for native mode required")
    version = subprocess.check_output([args.solver, "--version"], text=True)
    if f"git: {args.expected_git}" not in version.splitlines():
        parser.error("candidate revision mismatch")
    args.out.mkdir(parents=True, exist_ok=False)
    write_json(
        args.out / "run.json",
        {
            "arguments": {
                k: str(v) if isinstance(v, Path) else v
                for k, v in vars(args).items()
                if k != "native_seed"
            },
            "version": version,
            "host": platform.node(),
            "slurm_job_id": os.environ.get("SLURM_JOB_ID"),
            "command": sys.argv,
            "solver_sha256": hashlib.sha256(Path(args.solver).read_bytes()).hexdigest(),
            "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "native_seeds": {
                str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in args.native_seed
            },
        },
    )
    seeds = [native_prefix(p) for p in args.native_seed]
    for index, commands in enumerate(seeds):
        original = "\n".join(raw for raw, _ in commands) + "\n"
        row = validate(original, args.out / f"original-seed-{index}", args)
        if row["outcome"] != "pass":
            write_json(args.out / "seed-gate-failure.json", row)
            print(json.dumps(row))
            return 1
    summary = {"sessions": 0, "pass": 0, "sat_models": 0, "queries": 0, "failures": []}
    for index in range(args.count):
        seed = args.seed + index
        text = (
            generated(seed)
            if args.kind == "generated"
            else mutate_native(seeds[index % len(seeds)], seed)
        )
        row = validate(text, args.out / f"session-{seed}", args)
        summary["sessions"] += 1
        summary["queries"] += len(row["queries"])
        summary["sat_models"] += sum(
            q["answer"] == "sat" and q["outcome"] == "pass" for q in row["queries"]
        )
        if row["outcome"] == "pass":
            summary["pass"] += 1
        else:
            summary["failures"].append({"seed": seed, "result": row})
            summary["untriaged_failure"] += 1
        write_json(args.out / "summary.json", summary)
        if summary["failures"]:
            print(json.dumps(summary))
            return 1
    print(json.dumps(summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
