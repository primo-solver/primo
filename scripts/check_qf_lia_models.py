#!/usr/bin/env python3
"""Validate Primo's QF_LIA models with z3, as a local stand-in for Dolmen.

**Dolmen is the project's model checker and this is not a replacement for it.**
`scripts/slurm_qf_lia_model_fuzz_gate.sbatch` runs `scripts/validate_model.py`
with `--dolmen-arg=--strict=false` on the cluster, and that remains the
authoritative check. Dolmen validates QF_LIA `Int` models perfectly well; the
`39/45 model-error` episode that script records was its strict-mode grammar
check rejecting FuzzSMT's `(* <let-bound-literal> <compound>)` terms, not any
inability to handle `Int`.

This exists for the narrower case where Dolmen is simply *absent* -- in a
development environment where `make contrib-dolmen` fails, `validate_model.py`
falls through to a z3 Real-only path and reports `validation-unavailable` for
every QF_LIA sat case, which is silence rather than a check. Rather than treat
that silence as a pass, this pins every declared constant to the value Primo
reported and asks z3 whether the original constraints still hold. A model that
does not satisfy them comes back `unsat`.

Written for QF_LIA stage 5 (patching), whose failure mode is a wrong model
rather than a wrong answer -- see docs/qf-lia-branch-and-bound.md.

Usage:
    python3 scripts/check_qf_lia_models.py --manifest FILES.txt [--timeout 10]
    python3 scripts/check_qf_lia_models.py a.smt2 b.smt2
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# `(define-fun x () Int 4)` and `(define-fun x () Int (- 4))`.
DEFINE_INT = re.compile(r"\(define-fun\s+(\S+)\s+\(\)\s+Int\s+(.+?)\)\s*$")
ANSWERS = ("sat", "unsat", "unknown")


def first_answer(text: str) -> str:
    return next((line.strip() for line in text.splitlines() if line.strip() in ANSWERS), "?")


def primo_model(solver: list[str], path: Path, timeout: float) -> tuple[str, dict[str, str]]:
    """Run Primo with models enabled; return its answer and Int assignments."""
    script = path.read_text(errors="replace")
    if "(get-model)" not in script:
        script = script.replace("(check-sat)", "(check-sat)\n(get-model)", 1)
    script = "(set-option :produce-models true)\n" + script
    try:
        completed = subprocess.run([*solver, "/dev/stdin"], input=script, text=True,
                                   capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return "timeout", {}
    model: dict[str, str] = {}
    for line in completed.stdout.splitlines():
        matched = DEFINE_INT.search(line.strip())
        if matched:
            model[matched.group(1)] = matched.group(2).strip()
    return first_answer(completed.stdout), model


def z3_confirms(z3: str, path: Path, model: dict[str, str], timeout: float) -> str:
    """Ask z3 whether the original constraints hold under Primo's assignment."""
    body = path.read_text(errors="replace")
    # Everything before the first (check-sat) is the declaration/constraint set;
    # anything after it is commands we are replacing.
    cut = body.find("(check-sat)")
    prefix = body if cut == -1 else body[:cut]
    pins = "\n".join(f"(assert (= {name} {value}))" for name, value in model.items())
    try:
        completed = subprocess.run([z3, "-in"], input=f"{prefix}\n{pins}\n(check-sat)\n",
                                   text=True, capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return "timeout"
    return first_answer(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", type=Path)
    parser.add_argument("--manifest", type=Path, help="newline-separated file list")
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--solver-arg", action="append", default=["--quiet"])
    parser.add_argument("--z3", default="contrib/local/bin/z3")
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    files = list(args.files)
    if args.manifest:
        files.extend(Path(line.strip()) for line in args.manifest.read_text().splitlines()
                     if line.strip())
    if not files:
        parser.error("no input files")

    solver = [args.solver, *args.solver_arg]
    counts: dict[str, int] = {}
    wrong: list[str] = []
    for path in files:
        answer, model = primo_model(solver, path, args.timeout)
        if answer != "sat":
            counts[answer] = counts.get(answer, 0) + 1
            continue
        if not model:
            counts["sat-no-int-model"] = counts.get("sat-no-int-model", 0) + 1
            continue
        verdict = z3_confirms(args.z3, path, model, args.timeout)
        key = f"sat/z3-{verdict}"
        counts[key] = counts.get(key, 0) + 1
        if verdict == "unsat":
            wrong.append(str(path))

    for key in sorted(counts):
        print(f"  {key}: {counts[key]}")
    if wrong:
        print("\nWRONG MODELS:")
        for path in wrong:
            print(f"  {path}")
        return 1
    print("\nno wrong models")
    return 0


if __name__ == "__main__":
    sys.exit(main())
