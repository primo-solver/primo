#!/usr/bin/env python3
"""Scoped UF correctness gate using Primo's Boolean generator and native prefixes."""

import argparse
import hashlib
import json
import os
import platform
import random
import subprocess
import sys
from pathlib import Path

from incremental_lra_gate import native_prefix
from qf_uf_boolean_fuzz import generate_case
from validate_incremental import command_spans, validate, write_json


def scoped(commands, seed, mutate=False):
    """Move assertions into nested scopes, then reuse them after retirement.

    Declarations retain their original order and definition environment. Native
    prefixes with scopes retain those scopes; only assertions are changed.
    Negation and duplication preserve typing, not satisfiability: Z3 is the oracle.
    """
    rng = random.Random(seed)
    result = []
    assertions = []
    frames = [[]]
    depth = 0
    for raw, head in commands:
        kind = head[0]
        if kind in {"set-info", "check-sat", "get-model", "exit"}:
            continue
        if kind == "push":
            depth += int(head[1])
            frames.extend([] for _ in range(int(head[1])))
        elif kind == "pop":
            depth -= int(head[1])
            if int(head[1]):
                del frames[-int(head[1]):]
        if kind == "assert":
            if mutate and rng.randrange(5) == 0:
                raw = "(assert (not " + raw[len("(assert"):].strip()[:-1] + "))"
            assertions.append(raw)
            frames[-1].append(raw)
            # Native definitions can depend on prior declarations, so keep their
            # entire original command ordering. Additional tests stay at this depth.
            if mutate:
                result.append(raw)
        else:
            result.append(raw)
    if mutate:
        assertions = [raw for frame in frames for raw in frame]
        result += ["(check-sat)", "(push 2)"]
    else:
        result += ["(check-sat)", "(push 2)", *assertions, "(check-sat)"]
    # Reuse a native/generated term while its declaration remains in scope.
    if assertions:
        atom = assertions[-1][len("(assert"):].strip()[:-1]
        result += [assertions[-1], f"(assert (not {atom}))", "(check-sat)"]
    result += ["(pop 1)", "(check-sat)", "(pop 1)", "(check-sat)"]
    if not mutate:
        result += ["(push 1)", *reversed(assertions), "(check-sat)", "(pop 1)"]
    # Fresh declarations, names retired and redeclared, and Boolean UF arguments
    # are exercised even when a seed's base is inconsistent.
    result += [f"(pop {depth})", "(reset-assertions)", "(push 1)",
               "(declare-sort GateU 0)", "(declare-fun gateF (Bool) GateU)",
               "(declare-const gateB Bool)",
               "(assert (distinct (gateF gateB) (gateF (not gateB))))",
               "(check-sat)", "(assert (= (gateF true) (gateF false)))",
               "(check-sat)", "(pop 1)", "(declare-sort GateU 0)",
               "(declare-const gateF GateU)", "(check-sat)", "(exit)"]
    return "\n".join(result) + "\n"


def generated(seed):
    text = generate_case(random.Random(seed), seed, "all")
    return scoped([(text[a:b], h) for a, b, h in command_spans(text)], seed)


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
    parser.add_argument("--timeout", type=float, default=20)
    parser.add_argument("--dolmen-timeout", type=float, default=30)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.count <= 0 or (args.kind == "native" and not args.native_seed):
        parser.error("positive count and native seeds for native mode required")
    version = subprocess.check_output([args.solver, "--version"], text=True)
    if f"git: {args.expected_git}" not in version.splitlines():
        parser.error("candidate revision mismatch")
    args.out.mkdir(parents=True, exist_ok=False)
    write_json(args.out / "run.json", {
        "command": sys.argv, "version": version, "host": platform.node(),
        "slurm_job_id": os.environ.get("SLURM_JOB_ID"),
        "solver_sha256": hashlib.sha256(Path(args.solver).read_bytes()).hexdigest(),
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "boolean_generator_sha256": hashlib.sha256(
            Path(__file__).with_name("qf_uf_boolean_fuzz.py").read_bytes()).hexdigest(),
        "reference": {"command": [args.ref, *args.ref_arg],
                      "sha256": hashlib.sha256(Path(args.ref).read_bytes()).hexdigest(),
                      "version": subprocess.check_output([args.ref, "--version"], text=True)},
        "dolmen": {"command": args.dolmen,
                   "sha256": hashlib.sha256(Path(args.dolmen).read_bytes()).hexdigest(),
                   "version": subprocess.check_output([args.dolmen, "--version"], text=True)},
        "native_seeds": {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in args.native_seed},
    })
    seeds = [native_prefix(p) for p in args.native_seed]
    summary = {"sessions": 0, "pass": 0, "queries": 0, "sat_models": 0,
               "untriaged_failure": 0, "failures": []}
    for index in range(args.count):
        seed = args.seed + index
        text = generated(seed) if args.kind == "generated" else scoped(
            seeds[index % len(seeds)], seed, mutate=True)
        row = validate(text, args.out / f"session-{seed}", args)
        summary["sessions"] += 1
        summary["queries"] += len(row["queries"])
        summary["sat_models"] += sum(q["answer"] == "sat" and q["outcome"] == "pass"
                                     for q in row["queries"])
        if row["outcome"] == "pass":
            summary["pass"] += 1
        else:
            summary["untriaged_failure"] += 1
            summary["failures"].append({"seed": seed, "result": row})
        write_json(args.out / "summary.json", summary)
        if summary["failures"]:
            print(json.dumps(summary))
            return 1
    print(json.dumps(summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
