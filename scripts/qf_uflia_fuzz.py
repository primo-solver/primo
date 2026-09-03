#!/usr/bin/env python3
"""Generate mixed QF_UFLIA cases and compare Primo with reference solvers."""

from __future__ import annotations

import argparse
import csv
import os
import random
import shutil
import subprocess
from pathlib import Path

RESULTS = {"sat", "unsat", "unknown"}

LIA_ATOMS = [
    "(<= 0 x)",
    "(<= x 2)",
    "(< x y)",
    "(<= (+ x y) 3)",
    "(<= (- x z) 1)",
    "(= y 0)",
    "(= z 1)",
    "(= w 2)",
    "(<= (* 2 x) (+ y 4))",
]

EUF_ATOMS = [
    "(= (f x) (f y))",
    "(= (f y) (f z))",
    "(= (g x y) (g y x))",
    "(p x)",
    "(p y)",
    "(q x y)",
    "(= (h b) x)",
    "(r b x)",
]

PURIFIED_ATOMS = [
    "(p (+ x 1))",
    "(q (+ x y) z)",
    "(= (f (+ x 1)) (f y))",
    "(<= (f x) (+ y 2))",
    "(< (+ (f x) z) 5)",
    "(= (g x y) (+ z 1))",
    "(= (h (p x)) (+ y 1))",
]


def with_polarity(rng: random.Random, atom: str) -> str:
    return atom if rng.randrange(2) == 0 else f"(not {atom})"


def declarations() -> str:
    return """(set-logic QF_UFLIA)
(declare-fun x () Int)
(declare-fun y () Int)
(declare-fun z () Int)
(declare-fun w () Int)
(declare-fun b () Bool)
(declare-fun f (Int) Int)
(declare-fun g (Int Int) Int)
(declare-fun h (Bool) Int)
(declare-fun p (Int) Bool)
(declare-fun q (Int Int) Bool)
(declare-fun r (Bool Int) Bool)
"""


def render(assertions: list[str]) -> str:
    body = "\n".join(f"(assert {assertion})" for assertion in assertions)
    return declarations() + body + "\n(check-sat)\n"


def generate_nonconvex_case(rng: random.Random, index: int) -> str:
    upper = 1 + rng.randrange(2)
    anchors = [("y", 0), ("z", 1), ("w", 2)][: upper + 1]
    assertions = ["(<= 0 x)", f"(<= x {upper})"]
    assertions.extend(f"(= {name} {value})" for name, value in anchors)

    # LIA entails a disjunction x=0 v ... v x=upper, but no single equality.
    # Congruence plus these result disequalities rejects every arrangement in
    # even cases. Odd cases omit one disequality and remain satisfiable.
    forbidden = anchors if index % 2 == 0 else anchors[:-1]
    assertions.extend(f"(not (= (f x) (f {name})))" for name, _ in forbidden)
    assertions.extend(["(or (p x) (not (p x)))", with_polarity(rng, "(r b x)")])
    return render(assertions)


def generate_random_case(rng: random.Random, index: int) -> str:
    assertions = [with_polarity(rng, atom) for atom in rng.sample(LIA_ATOMS, 3)]
    assertions.extend(with_polarity(rng, atom) for atom in rng.sample(EUF_ATOMS, 3))
    assertions.extend(
        with_polarity(rng, atom) for atom in rng.sample(PURIFIED_ATOMS, 2)
    )
    if index % 5 == 0:
        assertions.extend(["(= x y)", "(not (= (f x) (f y)))"])
    if index % 7 == 0:
        assertions.extend(["(= (+ x 1) y)", "(p (+ x 1))", "(not (p y))"])
    if index % 11 == 0:
        assertions.extend(["(= b (p x))", "(not (= (h b) (h (p x))))"])
    return render(assertions)


def generate_case(rng: random.Random, index: int, shape: str) -> str:
    if shape == "nonconvex":
        return generate_nonconvex_case(rng, index)
    if shape == "random":
        return generate_random_case(rng, index)
    return (
        generate_nonconvex_case(rng, index)
        if index % 3 == 0
        else generate_random_case(rng, index)
    )


def run_solver(command: list[str], timeout: float) -> tuple[str, int]:
    try:
        proc = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", 124
    except FileNotFoundError:
        return "MISSING", 127

    stdout = proc.stdout.strip()
    first = stdout.splitlines()[0] if stdout else ""
    combined = (proc.stdout + "\n" + proc.stderr).lower()
    if "unsupported" in combined:
        return "UNSUPPORTED_INPUT", proc.returncode
    if proc.returncode != 0:
        return "CRASH", proc.returncode
    if first in RESULTS:
        return first, proc.returncode
    return "UNKNOWN_OUTPUT", proc.returncode


def classify(ours: str, refs: list[str]) -> str:
    unusable = {"CRASH", "MISSING", "TIMEOUT", "UNSUPPORTED_INPUT", "UNKNOWN_OUTPUT"}
    concrete_refs = [result for result in refs if result in {"sat", "unsat"}]
    if ours in unusable:
        return ours
    if any(result in unusable for result in refs):
        return "REFERENCE_UNUSABLE"
    if len(set(concrete_refs)) > 1:
        return "REF_DISAGREEMENT"
    if not concrete_refs or ours == "unknown":
        return "UNKNOWN_INVOLVED"
    if ours == concrete_refs[0]:
        return "PASS"
    return "OUR_SAT_REF_UNSAT" if ours == "sat" else "OUR_UNSAT_REF_SAT"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--ref", action="append", default=[])
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=20260901)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--shape", choices=["mixed", "nonconvex", "random"], default="mixed"
    )
    args = parser.parse_args()

    reference_names = args.ref or ["z3", "cvc5"]
    references: list[str] = []
    for reference in reference_names:
        resolved = shutil.which(reference)
        if resolved is None:
            print(f"required reference solver is unavailable: {reference}")
            return 2
        references.append(resolved)

    rng = random.Random(args.seed)
    cases_dir = args.out / "cases"
    bugs_dir = args.out / "bugs"
    cases_dir.mkdir(parents=True, exist_ok=True)
    bugs_dir.mkdir(parents=True, exist_ok=True)
    summary_path = args.out / "summary.csv"
    counts: dict[str, int] = {}
    fieldnames = ["case", "ours", "ours_code", "references", "outcome"]
    with summary_path.open("w", newline="", encoding="utf-8") as summary:
        writer = csv.DictWriter(summary, fieldnames=fieldnames)
        writer.writeheader()
        summary.flush()
        os.fsync(summary.fileno())
        for index in range(args.count):
            case_path = cases_dir / f"case-{index:05d}.smt2"
            case_path.write_text(
                generate_case(rng, index, args.shape), encoding="utf-8"
            )
            ours, ours_code = run_solver(
                [args.solver, *args.solver_arg, str(case_path)], args.timeout
            )
            ref_results = [
                run_solver([reference, str(case_path)], args.timeout)[0]
                for reference in references
            ]
            outcome = classify(ours, ref_results)
            counts[outcome] = counts.get(outcome, 0) + 1
            writer.writerow(
                {
                    "case": str(case_path),
                    "ours": ours,
                    "ours_code": ours_code,
                    "references": " ".join(ref_results),
                    "outcome": outcome,
                }
            )
            summary.flush()
            os.fsync(summary.fileno())
            if outcome not in {"PASS", "UNKNOWN_INVOLVED"}:
                shutil.copy2(case_path, bugs_dir / case_path.name)
            print(
                f"{case_path}: {outcome} ours={ours} refs={' '.join(ref_results)}",
                flush=True,
            )

    print(f"summary: {summary_path}")
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}")
    blocking = {
        "CRASH",
        "UNSUPPORTED_INPUT",
        "UNKNOWN_OUTPUT",
        "MISSING",
        "REFERENCE_UNUSABLE",
        "REF_DISAGREEMENT",
        "OUR_SAT_REF_UNSAT",
        "OUR_UNSAT_REF_SAT",
    }
    return 1 if any(counts.get(outcome, 0) for outcome in blocking) else 0


if __name__ == "__main__":
    raise SystemExit(main())
