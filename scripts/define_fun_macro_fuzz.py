#!/usr/bin/env python3
"""Generate non-recursive define-fun macro DAGs and compare Primo with Z3."""

from __future__ import annotations

import argparse
import csv
import random
import shutil
import subprocess
from pathlib import Path


CHECKABLE = {"sat", "unsat", "unknown"}
BAD_OUTCOMES = {
    "CRASH",
    "OUR_SAT_REF_UNSAT",
    "OUR_UNSAT_REF_SAT",
    "REFERENCE_UNUSABLE",
    "TIMEOUT",
    "UNKNOWN_OUTPUT",
    "UNSUPPORTED_INPUT",
}


def run_solver(command: list[str], timeout: float) -> tuple[str, int]:
    try:
        proc = subprocess.run(
            command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", 124
    except FileNotFoundError:
        return "MISSING", 127

    first = proc.stdout.strip().splitlines()[0] if proc.stdout.strip() else ""
    text = (proc.stdout + "\n" + proc.stderr).lower()
    if "unsupported" in text:
        return "UNSUPPORTED_INPUT", proc.returncode
    if first in CHECKABLE:
        return first, proc.returncode
    if proc.returncode != 0:
        return "CRASH", proc.returncode
    return "UNKNOWN_OUTPUT", proc.returncode


def classify(ours: str, reference: str) -> str:
    if ours in {"CRASH", "TIMEOUT", "UNKNOWN_OUTPUT", "UNSUPPORTED_INPUT"}:
        return ours
    if reference in {"CRASH", "MISSING", "TIMEOUT", "UNKNOWN_OUTPUT", "UNSUPPORTED_INPUT"}:
        return "REFERENCE_UNUSABLE"
    if ours == "unknown" or reference == "unknown":
        return "UNKNOWN_INVOLVED"
    if ours == reference:
        return "PASS"
    if ours == "sat" and reference == "unsat":
        return "OUR_SAT_REF_UNSAT"
    if ours == "unsat" and reference == "sat":
        return "OUR_UNSAT_REF_SAT"
    return "UNKNOWN_INVOLVED"


def random_term(rng: random.Random, names: list[str], depth: int) -> str:
    if depth <= 0 or rng.randrange(5) == 0:
        return rng.choice(["a", "b", "c", *names])
    if rng.randrange(2) == 0:
        return f"(f {random_term(rng, names, depth - 1)})"
    return f"(g {random_term(rng, names, depth - 1)} {random_term(rng, names, depth - 1)})"


def random_atom(rng: random.Random, names: list[str]) -> str:
    lhs = random_term(rng, names, 2)
    rhs = random_term(rng, names, 2)
    atom = f"(= {lhs} {rhs})" if rng.randrange(3) else f"(p {lhs})"
    if rng.randrange(2):
        return atom
    return f"(not {atom})"


def generate_case(rng: random.Random, index: int) -> str:
    definitions: list[str] = []
    names: list[str] = []
    for i in range(3 + rng.randrange(10)):
        name = f"d{i}"
        body = random_term(rng, names, 2)
        definitions.append(f"(define-fun {name} () A {body})")
        names.append(name)

    bool_names: list[str] = []
    for i in range(1 + rng.randrange(5)):
        name = f"b{i}"
        body = random_atom(rng, names)
        definitions.append(f"(define-fun {name} () Bool {body})")
        bool_names.append(name)

    atoms = [random_atom(rng, names) for _ in range(1 + rng.randrange(5))]
    atoms.extend(rng.sample(bool_names, k=rng.randrange(len(bool_names) + 1)))
    if index % 17 == 0:
        atoms.extend(["(= a b)", "(= b c)", "(not (= a c))"])
    if index % 23 == 0 and names:
        atoms.extend([f"(= {names[-1]} a)", f"(not (= {names[-1]} a))"])
    body = "\n".join(f"  (assert {atom})" for atom in atoms)
    defs = "\n".join(definitions)
    return f"""(set-logic QF_UF)
(declare-sort A 0)
(declare-fun a () A)
(declare-fun b () A)
(declare-fun c () A)
(declare-fun f (A) A)
(declare-fun g (A A) A)
(declare-fun p (A) Bool)
(define-fun same ((x A) (y A)) Bool (= x y))
{defs}
{body}
(check-sat)
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--ref", default="z3")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--seed", type=int, default=20260731)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out", type=Path, default=Path("fuzz-results/define-fun-macro"))
    parser.add_argument("--solver-arg", action="append", default=[])
    args = parser.parse_args()

    ref = shutil.which(args.ref) or args.ref
    rng = random.Random(args.seed)
    cases_dir = args.out / "cases"
    cases_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    counts: dict[str, int] = {}

    for index in range(args.count):
        case_path = cases_dir / f"case-{index:04d}.smt2"
        case_path.write_text(generate_case(rng, index), encoding="utf-8")
        ours, ours_code = run_solver([args.solver, *args.solver_arg, str(case_path)], args.timeout)
        reference, ref_code = run_solver([ref, str(case_path)], args.timeout)
        outcome = classify(ours, reference)
        counts[outcome] = counts.get(outcome, 0) + 1
        rows.append(
            {
                "case": str(case_path),
                "ours": ours,
                "ours_code": ours_code,
                "reference": reference,
                "reference_code": ref_code,
                "outcome": outcome,
            }
        )
        print(f"{case_path}: {outcome} ours={ours} ref={reference}")

    summary_path = args.out / "summary.csv"
    with summary_path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["case", "ours", "ours_code", "reference", "reference_code", "outcome"],
        )
        writer.writeheader()
        writer.writerows(rows)

    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}")
    print(f"summary: {summary_path}")
    return 1 if any(counts.get(outcome, 0) for outcome in BAD_OUTCOMES) else 0


if __name__ == "__main__":
    raise SystemExit(main())
