#!/usr/bin/env python3
"""Generate QF_UFLRA smoke cases and compare Primo with Z3."""

from __future__ import annotations

import argparse
import csv
import random
import shutil
import subprocess
from pathlib import Path


RESULTS = {"sat", "unsat", "unknown"}


EUF_ATOMS = [
    "(= a b)",
    "(= b c)",
    "(= a c)",
    "(= (f a) (f b))",
    "(= (f b) (f c))",
    "(p a)",
    "(p b)",
]

LRA_ATOMS = [
    "(< 0 x)",
    "(< x 4)",
    "(< y 3)",
    "(< (+ x y) 5)",
    "(<= (- x y) 2)",
    "(< x 0)",
    "(< 2 x)",
    "(< 1 y)",
    "(< y 1)",
]


def atom_with_polarity(rng: random.Random, atom: str) -> str:
    if rng.randrange(2) == 0:
        return atom
    return f"(not {atom})"


PURIFIED_EUF_ATOMS = [
    "(= a b)",
    "(= b c)",
    "(= (h a) (h b))",
    "(p a)",
    "(p (h b))",
]

PURIFIED_LRA_ATOMS = [
    "(< (r a) 3)",
    "(< 0 (r b))",
    "(< (+ (r a) x) 5)",
    "(<= (- (s (h a)) y) 2)",
    "(< (+ (r a) (r b)) 4)",
    "(< (s c) (r a))",
    "(< x (r c))",
]


SHARED_REAL_EUF_ATOMS = [
    "(p x)",
    "(p y)",
    "(p z)",
    "(q x y)",
    "(q y x)",
]

SHARED_REAL_LRA_ATOMS = [
    "(<= x y)",
    "(<= y x)",
    "(< x y)",
    "(< y z)",
    "(<= z x)",
    "(< 0 x)",
    "(< y 4)",
    "(< z 5)",
]


def generate_separable_case(rng: random.Random, index: int) -> str:
    euf_count = 1 + rng.randrange(4)
    lra_count = 1 + rng.randrange(4)
    euf_atoms = rng.sample(EUF_ATOMS, euf_count)
    lra_atoms = rng.sample(LRA_ATOMS, lra_count)

    assertions = [atom_with_polarity(rng, atom) for atom in euf_atoms]
    assertions.extend(atom_with_polarity(rng, atom) for atom in lra_atoms)

    if index % 10 == 0:
        assertions.extend(["(= a b)", "(= b c)", "(not (= a c))"])
    if index % 14 == 0:
        assertions.extend(["(< x 0)", "(< 2 x)"])

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-sort U 0)
(declare-fun a () U)
(declare-fun b () U)
(declare-fun c () U)
(declare-fun f (U) U)
(declare-fun p (U) Bool)
(declare-fun x () Real)
(declare-fun y () Real)
{body}
(check-sat)
"""


def generate_purified_case(rng: random.Random, index: int) -> str:
    euf_count = 1 + rng.randrange(4)
    lra_count = 1 + rng.randrange(4)
    euf_atoms = rng.sample(PURIFIED_EUF_ATOMS, euf_count)
    lra_atoms = rng.sample(PURIFIED_LRA_ATOMS, lra_count)

    assertions = [atom_with_polarity(rng, atom) for atom in euf_atoms]
    assertions.extend(atom_with_polarity(rng, atom) for atom in lra_atoms)

    if index % 9 == 0:
        assertions.extend(["(= a b)", "(< (r a) 0)", "(< 1 (r b))"])
    if index % 13 == 0:
        assertions.extend(["(not (= a b))", "(= (h a) (h b))"])

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-sort U 0)
(declare-fun a () U)
(declare-fun b () U)
(declare-fun c () U)
(declare-fun h (U) U)
(declare-fun p (U) Bool)
(declare-fun r (U) Real)
(declare-fun s (U) Real)
(declare-fun x () Real)
(declare-fun y () Real)
{body}
(check-sat)
"""


def generate_shared_real_case(rng: random.Random, index: int) -> str:
    euf_count = 1 + rng.randrange(4)
    lra_count = 1 + rng.randrange(4)
    euf_atoms = rng.sample(SHARED_REAL_EUF_ATOMS, euf_count)
    lra_atoms = rng.sample(SHARED_REAL_LRA_ATOMS, lra_count)

    assertions = [atom_with_polarity(rng, atom) for atom in euf_atoms]
    assertions.extend(atom_with_polarity(rng, atom) for atom in lra_atoms)

    if index % 8 == 0:
        assertions.extend(["(<= x y)", "(<= y x)", "(p x)", "(not (p y))"])
    if index % 11 == 0:
        assertions.extend(["(< x y)", "(p x)", "(not (p y))"])
    if index % 17 == 0:
        assertions.extend(["(<= y z)", "(<= z y)", "(q x y)", "(not (q x z))"])

    body = "\n".join(f"  (assert {assertion})" for assertion in assertions)
    return f"""(set-logic QF_UFLRA)
(declare-fun x () Real)
(declare-fun y () Real)
(declare-fun z () Real)
(declare-fun p (Real) Bool)
(declare-fun q (Real Real) Bool)
{body}
(check-sat)
"""


def generate_case(rng: random.Random, index: int, shape: str) -> str:
    if shape == "separable":
        return generate_separable_case(rng, index)
    if shape == "purified":
        return generate_purified_case(rng, index)
    if shape == "shared-real":
        return generate_shared_real_case(rng, index)
    choice = rng.randrange(3)
    if choice == 0:
        return generate_separable_case(rng, index)
    if choice == 1:
        return generate_purified_case(rng, index)
    return generate_shared_real_case(rng, index)


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
    if first in RESULTS:
        return first, proc.returncode
    if proc.returncode != 0:
        return "CRASH", proc.returncode
    return "UNKNOWN_OUTPUT", proc.returncode


def classify(ours: str, ref: str) -> str:
    if ours == "UNSUPPORTED_INPUT":
        return "UNSUPPORTED_INPUT"
    if ours == "CRASH":
        return "CRASH"
    if ours == "TIMEOUT":
        return "TIMEOUT"
    if ref in {"MISSING", "TIMEOUT", "CRASH", "UNKNOWN_OUTPUT", "UNSUPPORTED_INPUT"}:
        return "REFERENCE_UNUSABLE"
    if ours == "unknown" or ref == "unknown":
        return "UNKNOWN_INVOLVED"
    if ours == ref:
        return "PASS"
    if ours == "sat" and ref == "unsat":
        return "OUR_SAT_REF_UNSAT"
    if ours == "unsat" and ref == "sat":
        return "OUR_UNSAT_REF_SAT"
    return "UNKNOWN_INVOLVED"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--ref", default="z3")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--seed", type=int, default=20260730)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out", type=Path, default=Path("fuzz-results/qf_uflra-smoke"))
    parser.add_argument(
        "--shape", choices=["separable", "purified", "shared-real", "mixed"], default="separable"
    )
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
        case_path.write_text(generate_case(rng, index, args.shape), encoding="utf-8")
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

    print(f"summary: {summary_path}")
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}")

    bad = {
        "UNSUPPORTED_INPUT",
        "CRASH",
        "OUR_SAT_REF_UNSAT",
        "OUR_UNSAT_REF_SAT",
        "UNKNOWN_OUTPUT",
        "REFERENCE_UNUSABLE",
    }
    return 1 if any(counts.get(outcome, 0) for outcome in bad) else 0


if __name__ == "__main__":
    raise SystemExit(main())
