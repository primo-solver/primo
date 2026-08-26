#!/usr/bin/env python3
"""Generate QF_UF cases exercising `Bool` in every capacity and compare with Z3.

SMT-LIB gives `Bool` no special status: it is a sort like any other, so an
uninterpreted function may take it, return it, or both, and equality is defined
on it. It also has a property no other sort has -- it is *finite, with exactly
two elements* -- which licenses inferences that ordinary congruence closure
does not perform.

Primo already had a `bool-arguments` shape in
`scripts/qf_uflra_smoke_fuzz.py` and eight seeds under
`benchmarks/seeds/qf_uflra/`. Every one of them is QF_UFLRA carrying a Real
constraint, because the semantics was fixed for the mixed path (commit
47fd382, "Fix Boolean argument congruence in mixed EUF"). Pure QF_UF is a
different solver path, and nothing tested that it had the same behaviour.
`bugs/qf-uf-boolean-argument-congruence/` is what that gap cost: a wrong
answer on a four-assertion formula with no arithmetic in it at all.

Two filters had to fail together for that to survive, and this generator is
built to defeat both:

  1. **Logic.** Everything here is `(set-logic QF_UF)` with no arithmetic
     anywhere, so it can only exercise the pure path.
  2. **Direction.** Congruence has a forward direction (`p = q` implies
     `f(p) = f(q)`) and a converse that holds only because `Bool` is
     two-valued (`f(p) = f(q)` with `f` injective implies `p = q`). The old
     seeds assert the premise and check the forward step. The shapes below
     deliberately force the *converse*, and force it to be derived rather
     than stated.

Shapes, and the capacity each covers:

  argument        Bool-sorted arguments of unary/binary/ternary functions,
                  including the constants `true`/`false`
  result          Bool-valued applications used directly as formulas and
                  compared with `=` / `distinct`
  compound        compound formulas (`and`, `or`, `not`, `xor`, `ite`) passed
                  as arguments
  nested          applications as arguments of applications, `f(f(p))`,
                  `g(f(p), q)`
  mixed-sort      Bool travelling through an uninterpreted sort: `U -> Bool`
                  and `Bool -> U` chains
  two-valued      inferences that need `|Bool| = 2`: the converse of
                  congruence, and pigeonhole over three distinct Bool terms
  converse        the specific shape the old harness could not express -- an
                  argument equality derivable ONLY from injectivity
  all             uniform mix of the above (default)

Exit code is nonzero if any case disagrees with the reference, crashes, or is
rejected as unsupported -- the same contract the other fuzz scripts use, so a
SLURM gate can consume it unchanged.

    python3 scripts/qf_uf_boolean_fuzz.py --count 400 --shape all \
        --solver ./build/primo --ref contrib/local/bin/z3
"""

from __future__ import annotations

import argparse
import csv
import os
import random
import subprocess
from pathlib import Path

RESULTS = {"sat", "unsat", "unknown"}

SHAPES = (
    "argument",
    "result",
    "compound",
    "nested",
    "mixed-sort",
    "two-valued",
    "converse",
    "all",
)

# Declarations every case shares. Kept fixed rather than randomized so a
# failing case reduces to something a human can read, and so two cases with
# the same assertions are literally the same file.
PREAMBLE = """(set-logic QF_UF)
(declare-sort U 0)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun r () Bool)
(declare-fun a () U)
(declare-fun b () U)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(declare-fun h (Bool Bool Bool) Bool)
(declare-fun tb (Bool) U)
(declare-fun fromu (U) Bool)
(declare-fun uu (U) U)
"""

BOOL_TERMS = ["p", "q", "r", "true", "false", "(f p)", "(f q)", "(fromu a)", "(fromu b)"]

COMPOUND_TERMS = [
    "(not p)",
    "(and p q)",
    "(or p q)",
    "(xor p q)",
    "(=> p q)",
    "(ite p q r)",
    "(and (not p) (or q r))",
    "(ite (f p) q (not r))",
]


def maybe_negate(rng: random.Random, atom: str) -> str:
    return atom if rng.randrange(2) else f"(not {atom})"


def argument_atoms(rng: random.Random) -> list[str]:
    """`Bool` in argument position, including the two constants."""
    terms = rng.sample(BOOL_TERMS, k=min(4, len(BOOL_TERMS)))
    atoms = [f"(f {terms[0]})", f"(g {terms[0]} {terms[1]})"]
    atoms.append(f"(h {terms[0]} {terms[1]} {terms[2]})")
    atoms.append(f"(= (f {terms[0]}) (f {terms[1]}))")
    atoms.append(f"(= (g {terms[0]} {terms[1]}) (g {terms[1]} {terms[0]}))")
    atoms.append("(distinct (f true) (f false))")
    return [maybe_negate(rng, atom) for atom in atoms]


def result_atoms(rng: random.Random) -> list[str]:
    """Bool-valued applications used as formulas and compared to each other."""
    atoms = [
        "(f p)",
        "(g p q)",
        "(= (f p) q)",
        "(= (f p) (g p q))",
        "(distinct (f p) (f q))",
        "(= (fromu a) (f p))",
    ]
    return [maybe_negate(rng, atom) for atom in atoms]


def compound_atoms(rng: random.Random) -> list[str]:
    """Compound formulas in argument position."""
    left, right = rng.sample(COMPOUND_TERMS, k=2)
    atoms = [
        f"(f {left})",
        f"(g {left} {right})",
        f"(= (f {left}) (f {right}))",
        f"(= (f {left}) (f p))",
        "(distinct (f true) (f false))",
    ]
    return [maybe_negate(rng, atom) for atom in atoms]


def nested_atoms(rng: random.Random) -> list[str]:
    """Applications feeding applications."""
    atoms = [
        "(f (f p))",
        "(= (f (f p)) (f p))",
        "(g (f p) (f q))",
        "(= (f (g p q)) (f (g q p)))",
        "(fromu (tb (f p)))",
        "(= (fromu (uu (tb p))) (f q))",
    ]
    return [maybe_negate(rng, atom) for atom in atoms]


def mixed_sort_atoms(rng: random.Random) -> list[str]:
    """Bool travelling through an uninterpreted sort and back."""
    atoms = [
        "(= (tb p) (tb q))",
        "(= (tb p) a)",
        "(distinct (tb true) (tb false))",
        "(fromu (tb p))",
        "(= (fromu a) (fromu b))",
        "(= (uu (tb p)) (tb q))",
    ]
    return [maybe_negate(rng, atom) for atom in atoms]


def two_valued_atoms(rng: random.Random) -> list[str]:
    """Inferences that need |Bool| = 2, not just congruence.

    Three pairwise-distinct Bool terms cannot exist; neither can an injective
    f together with three distinct images. A solver reasoning about Bool as an
    arbitrary uninterpreted sort accepts both.
    """
    x, y, z = rng.sample(["p", "q", "r", "(f p)", "(f q)", "(fromu a)"], k=3)
    atoms = [
        f"(distinct {x} {y})",
        f"(distinct {y} {z})",
        f"(distinct {x} {z})",
        "(distinct (f true) (f false))",
        f"(= (f {x}) (f {y}))",
    ]
    return atoms


def converse_case(rng: random.Random) -> list[str]:
    """The shape the QF_UFLRA seeds could not express.

    `f` is injective on Bool, so `f(x) = f(y)` forces `x = y`. The point is
    that the argument equality is never asserted and is not derivable
    propositionally: the only route to it is two-valuedness plus injectivity.
    A second function then turns that equality into a contradiction through
    ordinary congruence.
    """
    x, y = rng.sample(["p", "q", "r"], k=2)
    witness = rng.choice(
        [
            # Contradiction through a Bool-valued binary application.
            [f"(= (f {x}) (f {y}))",
             "(distinct (f true) (f false))",
             f"(g {x} {y})",
             f"(not (g {y} {y}))"],
            # Contradiction through an uninterpreted-sort image.
            [f"(= (f {x}) (f {y}))",
             "(distinct (f true) (f false))",
             f"(distinct (tb {x}) (tb {y}))"],
            # Contradiction through a nested application.
            [f"(= (f {x}) (f {y}))",
             "(distinct (f true) (f false))",
             f"(distinct (f (f {x})) (f (f {y})))"],
            # Three-argument version: the equality must reach h's congruence.
            [f"(= (f {x}) (f {y}))",
             "(distinct (f true) (f false))",
             f"(h {x} {y} {x})",
             f"(not (h {y} {y} {y}))"],
        ]
    )
    return witness


ATOM_BUILDERS = {
    "argument": argument_atoms,
    "result": result_atoms,
    "compound": compound_atoms,
    "nested": nested_atoms,
    "mixed-sort": mixed_sort_atoms,
    "two-valued": two_valued_atoms,
}


def generate_case(rng: random.Random, index: int, shape: str) -> str:
    if shape == "all":
        shape = rng.choice([name for name in SHAPES if name != "all"])

    if shape == "converse":
        # Emitted verbatim: every assertion is load-bearing, and dropping or
        # negating one turns the intended unsat into a satisfiable formula
        # that proves nothing.
        assertions = converse_case(rng)
    else:
        atoms = ATOM_BUILDERS[shape](rng)
        keep = rng.randint(2, len(atoms))
        assertions = rng.sample(atoms, k=keep)
        # Mix in a clause so the SAT solver has real case analysis to do
        # rather than a conjunction of units.
        if rng.randrange(2) and len(atoms) >= 2:
            left, right = rng.sample(atoms, k=2)
            assertions.append(f"(or {left} {right})")

    body = "\n".join(f"(assert {atom})" for atom in assertions)
    return f"; qf_uf boolean fuzz case {index}, shape {shape}\n{PREAMBLE}{body}\n(check-sat)\n"


def run_solver(command: list[str], timeout: float) -> tuple[str, int]:
    try:
        proc = subprocess.run(
            command, check=False, text=True, capture_output=True, timeout=timeout
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
    if ours in {"UNSUPPORTED_INPUT", "CRASH", "TIMEOUT"}:
        return ours
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
    parser.add_argument("--ref", default="contrib/local/bin/z3")
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--out", default="fuzz-results/qf-uf-boolean")
    parser.add_argument("--shape", choices=SHAPES, default="all")
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument(
        "--keep-passing",
        action="store_true",
        help="keep every generated case; by default only disagreements are kept",
    )
    args = parser.parse_args()

    out_dir = Path(args.out)
    cases_dir = out_dir / "cases"
    cases_dir.mkdir(parents=True, exist_ok=True)
    summary_path = out_dir / "summary.csv"

    rng = random.Random(args.seed)
    counts: dict[str, int] = {}
    with summary_path.open("w", newline="", encoding="utf-8") as summary:
        writer = csv.DictWriter(
            summary,
            fieldnames=["case", "shape", "ours", "ours_code", "reference", "reference_code", "outcome"],
        )
        writer.writeheader()
        for index in range(args.count):
            shape = args.shape
            case_path = cases_dir / f"case-{index:04d}.smt2"
            text = generate_case(rng, index, shape)
            case_path.write_text(text, encoding="utf-8")
            recorded_shape = text.splitlines()[0].rsplit(" ", 1)[-1]
            ours, ours_code = run_solver(
                [args.solver, *args.solver_arg, str(case_path)], args.timeout
            )
            reference, ref_code = run_solver([args.ref, str(case_path)], args.timeout)
            outcome = classify(ours, reference)
            counts[outcome] = counts.get(outcome, 0) + 1
            writer.writerow(
                {
                    "case": str(case_path),
                    "shape": recorded_shape,
                    "ours": ours,
                    "ours_code": ours_code,
                    "reference": reference,
                    "reference_code": ref_code,
                    "outcome": outcome,
                }
            )
            summary.flush()
            os.fsync(summary.fileno())
            if outcome == "PASS" and not args.keep_passing:
                case_path.unlink()
            else:
                print(f"{case_path}: {outcome} ours={ours} ref={reference}", flush=True)

    print(f"summary: {summary_path}", flush=True)
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}", flush=True)

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
