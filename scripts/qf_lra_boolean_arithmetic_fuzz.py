#!/usr/bin/env python3
"""Randomized QF_LRA fuzzing for formulas that mix free Boolean atoms with
linear arithmetic chains/fans -- a shape FuzzSMT never generates for QF_LRA
(its predicate/UF-arity flags apply only to the QF_UF* logics), so the
mutation seed corpus and every FuzzSMT-driven gate in this project never
exercise it either. See docs/fuzz-coverage-qf-lra.md's
"A free-Boolean-mixed-with-arithmetic blind spot" section for how that gap
let a real model-construction bug through (build_qf_lra_model dropping
Boolean atoms the SAT search, rather than preprocessing, decided).

Every case is a chain or fan of strict Le/Lt atoms over fresh Real
variables, satisfiable by construction (an acyclic total-order chain always
has room), optionally combined with a Boolean guard shape. Shapes:

  chain               v0 < v1 < ... < vk, random length. Only the two
                      endpoints are individually monotone; interior
                      variables are genuinely disqualified and, depending
                      on length, either keep a real tableau row or vanish
                      to zero during --monotone-elimination reconstruction.
  chain-offsets       same, with (< (+ vi c) v{i+1})-style positive integer
                      offsets on a random subset of atoms.
  fan-consistent      a hub pushed the same direction by every leaf (hub
                      stays individually monotone).
  fan-disqualified    a hub pushed opposite directions by different leaves
                      (hub disqualified, vanishes under elimination).
  equality-disqualifier  a chain plus (= vi c) on one variable (disqualified
                      via a *surviving* real constraint, not a vanish).
  polarity-not        chain atoms rewritten via the logically-equivalent
                      negated form, exercising polarity tracking without
                      changing satisfiability.
  both-polarities     (and (or L p) (or (not L) q)) on one chain atom --
                      the exact shape that found the bug this generator
                      exists to catch.

Run with --monotone-elimination off (default) it is general QF_LRA
Boolean+arithmetic model coverage; passed as a --solver-arg it becomes the
--monotone-elimination reconstruction stress test.
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import json
import os
import random
import shutil
import subprocess
from pathlib import Path

RESULTS = {"sat", "unsat", "unknown"}
SHAPES = (
    "chain",
    "chain-offsets",
    "fan-consistent",
    "fan-disqualified",
    "equality-disqualifier",
    "polarity-not",
    "both-polarities",
)
STAT_CANDIDATES = ":preprocessing.monotone-elimination.candidates"
STAT_ATOMS_FOLDED = ":preprocessing.monotone-elimination.atoms-folded"

# Shapes guaranteed by construction to leave at least one eligible variable
# for --monotone-elimination (a chain's two endpoints, or a fan's hub/leaves).
ACTIVATION_SHAPES = frozenset(SHAPES) - {"polarity-not"}


@dataclasses.dataclass(frozen=True)
class GeneratedCase:
    shape: str
    text: str
    activation_expected: bool


@dataclasses.dataclass(frozen=True)
class SolverRun:
    answer: str
    returncode: int
    stats: dict[str, int]
    stdout: str = ""


def preamble(extra_bools: list[str]) -> str:
    lines = ["(set-logic QF_LRA)"]
    lines.extend(f"(declare-fun {b} () Bool)" for b in extra_bools)
    return "\n".join(lines) + "\n"


def chain_vars_and_atoms(
    rng: random.Random, min_len: int, max_len: int, with_offsets: bool
) -> tuple[list[str], list[str]]:
    length = rng.randint(min_len, max_len)
    variables = [f"v{i}" for i in range(length + 1)]
    atoms = []
    for i in range(length):
        if with_offsets and rng.random() < 0.5:
            offset = rng.choice([1, 2, 5, 10])
            atoms.append(f"(< (+ {variables[i]} {offset}) {variables[i + 1]})")
        else:
            atoms.append(f"(< {variables[i]} {variables[i + 1]})")
    return variables, atoms


def build_chain(rng: random.Random, with_offsets: bool) -> GeneratedCase:
    variables, atoms = chain_vars_and_atoms(rng, 2, 6, with_offsets)
    declares = "".join(f"(declare-fun {v} () Real)\n" for v in variables)
    body = "(and " + " ".join(atoms) + ")" if len(atoms) > 1 else atoms[0]
    shape = "chain-offsets" if with_offsets else "chain"
    text = (
        preamble([])
        + declares
        + f"(assert {body})\n"
        + "(check-sat)\n"
    )
    return GeneratedCase(shape=shape, text=text, activation_expected=True)


def build_fan(rng: random.Random, consistent: bool) -> GeneratedCase:
    leaf_count = rng.randint(2, 4)
    hub = "h"
    leaves = [f"l{i}" for i in range(leaf_count)]
    atoms = []
    for i, leaf in enumerate(leaves):
        # Consistent: hub always the smaller side. Disqualified: alternate,
        # so the hub is pushed both directions while each leaf, appearing
        # only once, stays individually monotone.
        push_up = consistent or i % 2 == 0
        atoms.append(f"(< {hub} {leaf})" if push_up else f"(< {leaf} {hub})")
    declares = "".join(f"(declare-fun {v} () Real)\n" for v in [hub, *leaves])
    body = "(and " + " ".join(atoms) + ")" if len(atoms) > 1 else atoms[0]
    shape = "fan-consistent" if consistent else "fan-disqualified"
    text = preamble([]) + declares + f"(assert {body})\n" + "(check-sat)\n"
    return GeneratedCase(shape=shape, text=text, activation_expected=True)


def build_equality_disqualifier(rng: random.Random) -> GeneratedCase:
    variables, atoms = chain_vars_and_atoms(rng, 2, 5, with_offsets=False)
    pinned = rng.choice(variables)
    constant = rng.choice([-5, -1, 0, 3, 7])
    atoms.append(f"(= {pinned} {constant})" if constant >= 0 else f"(= {pinned} (- {-constant}))")
    declares = "".join(f"(declare-fun {v} () Real)\n" for v in variables)
    body = "(and " + " ".join(atoms) + ")"
    text = preamble([]) + declares + f"(assert {body})\n" + "(check-sat)\n"
    return GeneratedCase(shape="equality-disqualifier", text=text, activation_expected=True)


def negate_equivalent(atom: str) -> str:
    # (< a b)  <=>  (not (<= b a))
    assert atom.startswith("(< ")
    inner = atom[3:-1]
    a, b = inner.split(" ", 1)
    if "(" in a or "(" in b:
        # A naive split on the first space only works for bare-token
        # operands; a nested/offset term like "(+ v0 5) v1" would silently
        # produce mismatched parens instead of a clear error.
        raise ValueError(f"negate_equivalent only supports simple operands: {atom!r}")
    return f"(not (<= {b} {a}))"


def build_polarity_not(rng: random.Random) -> GeneratedCase:
    variables, atoms = chain_vars_and_atoms(rng, 2, 6, with_offsets=False)
    rewritten = [negate_equivalent(atom) if rng.random() < 0.5 else atom for atom in atoms]
    declares = "".join(f"(declare-fun {v} () Real)\n" for v in variables)
    body = "(and " + " ".join(rewritten) + ")" if len(rewritten) > 1 else rewritten[0]
    text = preamble([]) + declares + f"(assert {body})\n" + "(check-sat)\n"
    return GeneratedCase(shape="polarity-not", text=text, activation_expected=True)


def build_both_polarities(rng: random.Random) -> GeneratedCase:
    variables, atoms = chain_vars_and_atoms(rng, 2, 5, with_offsets=False)
    target_index = rng.randrange(len(atoms))
    literal = atoms[target_index]
    p, q = "p", "q"
    guarded = [f"(or {literal} {p})", f"(or (not {literal}) {q})"]
    other_atoms = [atom for i, atom in enumerate(atoms) if i != target_index]
    all_conjuncts = guarded + other_atoms
    declares = "".join(f"(declare-fun {v} () Real)\n" for v in variables)
    body = "(and " + " ".join(all_conjuncts) + ")"
    text = preamble([p, q]) + declares + f"(assert {body})\n" + "(check-sat)\n"
    return GeneratedCase(shape="both-polarities", text=text, activation_expected=True)


def generate_case(rng: random.Random, index: int) -> GeneratedCase:
    shape = SHAPES[index % len(SHAPES)]
    if shape == "chain":
        return build_chain(rng, with_offsets=False)
    if shape == "chain-offsets":
        return build_chain(rng, with_offsets=True)
    if shape == "fan-consistent":
        return build_fan(rng, consistent=True)
    if shape == "fan-disqualified":
        return build_fan(rng, consistent=False)
    if shape == "equality-disqualifier":
        return build_equality_disqualifier(rng)
    if shape == "polarity-not":
        return build_polarity_not(rng)
    if shape == "both-polarities":
        return build_both_polarities(rng)
    raise ValueError(f"unknown shape: {shape}")


def preflight_generation(seed: int, count: int) -> list[GeneratedCase]:
    rng = random.Random(seed)
    return [generate_case(rng, index) for index in range(count)]


def parse_stats(output: str) -> dict[str, int]:
    stats: dict[str, int] = {}
    for line in output.splitlines():
        fields = line.strip().split()
        if len(fields) != 2 or not fields[0].startswith(":"):
            continue
        try:
            stats[fields[0]] = int(fields[1])
        except ValueError:
            continue
    return stats


def run_command(command: list[str], timeout: float, input_text: str | None = None):
    try:
        proc = subprocess.run(
            command, check=False, text=True, input=input_text, capture_output=True, timeout=timeout
        )
        return "done", proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired:
        return "timeout", None, "", ""
    except FileNotFoundError:
        return "missing", 127, "", ""


def run_solver(command: list[str], timeout: float, collect_stats: bool) -> SolverRun:
    status, returncode, stdout, stderr = run_command(command, timeout)
    if status == "timeout":
        return SolverRun("TIMEOUT", 124, {})
    if status == "missing":
        return SolverRun("MISSING", 127, {})
    output = f"{stdout}\n{stderr}"
    lowered = output.lower()
    answer = next((line.strip() for line in stdout.splitlines() if line.strip() in RESULTS), "")
    # Only classify from stray "unsupported"/"(error" substrings when no
    # clean result line was already found -- a real answer should never be
    # overridden by incidental text elsewhere in stdout/stderr.
    if not answer:
        if "unsupported" in lowered:
            answer = "UNSUPPORTED_INPUT"
        elif "(error" in lowered:
            answer = "SOLVER_ERROR"
        else:
            answer = "CRASH" if returncode != 0 else "UNKNOWN_OUTPUT"
    return SolverRun(answer, returncode, parse_stats(output) if collect_stats else {}, stdout)


def extract_real_and_bool_assignments(model_text: str) -> list[tuple[str, str]]:
    import re

    assignments: list[tuple[str, str]] = []
    for pattern, sort in (
        (r"\(define-fun (\S+) \(\) Real (\([^()]*(?:\([^()]*\)[^()]*)*\)|[^()\s]+)\)", "Real"),
        (r"\(define-fun (\S+) \(\) Bool (true|false)\)", "Bool"),
    ):
        assignments.extend(re.findall(pattern, model_text))
    return assignments


def without_model_commands(input_text: str) -> str:
    lines: list[str] = []
    for line in input_text.splitlines():
        stripped = line.strip()
        if stripped == "(get-model)" or stripped.startswith("(set-option :produce-models"):
            continue
        lines.append(line)
    return "\n".join(lines) + "\n"


def add_assignments_before_check_sat(input_text: str, assignments: list[tuple[str, str]]) -> str:
    assignment_text = "".join(f"(assert (= {symbol} {value}))\n" for symbol, value in assignments)
    marker = "(check-sat)"
    index = input_text.rfind(marker)
    if index == -1:
        return input_text.rstrip() + "\n" + assignment_text + marker + "\n"
    return input_text[:index] + assignment_text + input_text[index:]


def validate_real_and_bool_assignments_with_z3(
    z3_path: str | None, original_text: str, assignments: list[tuple[str, str]], timeout: float
) -> tuple[str, int | None, str, str]:
    if z3_path is None or not assignments:
        return "unavailable", None, "", "Z3 fallback unavailable\n"
    validation_input = add_assignments_before_check_sat(
        without_model_commands(original_text), assignments
    )
    return run_command([z3_path, "-smt2", "-in"], timeout, validation_input)


def validate_model_with_dolmen(
    dolmen_path: str | None,
    z3_path: str | None,
    original_text: str,
    solver_response: str,
    out_dir: Path,
    stem: str,
    timeout: float,
) -> str:
    if dolmen_path is None:
        return "DOLMEN_MISSING"
    problem_text = original_text
    if ":produce-models" not in problem_text:
        problem_text = "(set-option :produce-models true)\n" + problem_text
    problem_path = out_dir / f"{stem}.smt2"
    response_path = out_dir / f"{stem}.rsmt2"
    problem_path.write_text(problem_text, encoding="utf-8")
    response_path.write_text(solver_response, encoding="utf-8")
    status, returncode, _stdout, stderr = run_command(
        [dolmen_path, "--check-model=true", "-r", str(response_path), str(problem_path)], timeout
    )
    if status == "timeout":
        return "DOLMEN_TIMEOUT"
    if status == "missing":
        return "DOLMEN_MISSING"
    if returncode == 0:
        return "PASS"
    # Dolmen sometimes rejects syntax Z3 accepts (see
    # validate_qfuf_models_with_dolmen.py's own fallback for the same
    # reason); re-check the extracted assignments against the original
    # formula with Z3 before giving up on the model.
    assignments = extract_real_and_bool_assignments(solver_response)
    z3_status, z3_returncode, z3_stdout, z3_stderr = validate_real_and_bool_assignments_with_z3(
        z3_path, original_text, assignments, timeout
    )
    if (
        z3_status == "done"
        and z3_returncode == 0
        and "(error" not in z3_stdout.lower()
        and "(error" not in z3_stderr.lower()
        and next((line.strip() for line in z3_stdout.splitlines() if line.strip() == "sat"), None)
    ):
        return "PASS"
    if "(error" in stderr.lower():
        return "DOLMEN_ERROR"
    return "MODEL_ERROR"


def activation_failure(case: GeneratedCase, stats: dict[str, int]) -> str | None:
    if case.shape not in ACTIVATION_SHAPES:
        return None
    missing = [name for name in (STAT_CANDIDATES, STAT_ATOMS_FOLDED) if stats.get(name, 0) <= 0]
    return ",".join(missing) if missing else None


def classify(
    case: GeneratedCase,
    ours: SolverRun,
    reference: SolverRun,
    dolmen_outcome: str | None,
    monotone_elimination: bool,
) -> str:
    if ours.answer in {"CRASH", "TIMEOUT", "MISSING", "UNKNOWN_OUTPUT", "UNSUPPORTED_INPUT", "SOLVER_ERROR"}:
        return ours.answer
    if reference.answer in {"TIMEOUT", "MISSING", "CRASH", "UNKNOWN_OUTPUT"}:
        return "REFERENCE_UNUSABLE"
    if ours.answer in ("sat", "unsat") and reference.answer in ("sat", "unsat") and ours.answer != reference.answer:
        return "REFERENCE_DISAGREEMENT"
    if ours.answer == "sat" and dolmen_outcome not in (None, "PASS"):
        return dolmen_outcome
    if monotone_elimination and ours.answer == "sat":
        missing = activation_failure(case, ours.stats)
        if missing:
            return "ACTIVATION_MISSING"
    return "PASS"


def write_metadata(path: Path, data: dict[str, object]) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def git_revision() -> str:
    proc = subprocess.run(["git", "rev-parse", "HEAD"], check=False, text=True, capture_output=True)
    return proc.stdout.strip() if proc.returncode == 0 else "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--z3", default="z3")
    parser.add_argument("--dolmen", default="dolmen")
    parser.add_argument("--count", type=int, default=len(SHAPES) * 20)
    parser.add_argument("--seed", type=int, default=2026081701)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--dolmen-timeout", type=float, default=10.0)
    parser.add_argument("--out", type=Path, default=Path("fuzz-results/qf-lra-boolean-arithmetic"))
    args = parser.parse_args()

    if args.count < len(SHAPES):
        parser.error(f"--count must be at least {len(SHAPES)} to cover every shape")
    # One cycle through every shape as a fail-fast smoke check -- not the
    # full --count, which the main loop below generates for real; a
    # generator crash surfaces here before any solver invocation runs.
    preflight_generation(args.seed, len(SHAPES))

    monotone_elimination = "--monotone-elimination" in args.solver_arg
    dolmen_path = shutil.which(args.dolmen)
    z3_path = shutil.which(args.z3) or args.z3

    args.out.mkdir(parents=True, exist_ok=True)
    cases_dir = args.out / "cases"
    failures_dir = args.out / "failures"
    cases_dir.mkdir(exist_ok=True)
    failures_dir.mkdir(exist_ok=True)

    metadata_path = args.out / "metadata.json"
    metadata: dict[str, object] = {
        "status": "running",
        "commit": git_revision(),
        "seed": args.seed,
        "count": args.count,
        "shapes": SHAPES,
        "solver": args.solver,
        "solver_args": args.solver_arg,
        "monotone_elimination": monotone_elimination,
        "dolmen": dolmen_path,
        "timeout_seconds": args.timeout,
    }
    write_metadata(metadata_path, metadata)

    rng = random.Random(args.seed)
    counts: dict[str, int] = {}
    summary_path = args.out / "summary.csv"
    fieldnames = ["case", "shape", "answer", "reference", "dolmen", "outcome"]
    with summary_path.open("w", newline="", encoding="utf-8") as summary:
        writer = csv.DictWriter(summary, fieldnames=fieldnames)
        writer.writeheader()
        summary.flush()
        os.fsync(summary.fileno())
        for index in range(args.count):
            case = generate_case(rng, index)
            stem = f"case-{index:05d}-{case.shape}"
            case_path = cases_dir / f"{stem}.smt2"
            # get-model after unsat/unknown is a harmless no-op, so this is
            # requested unconditionally -- one solver invocation per case
            # covers both the answer/stats and, when sat, the model dolmen
            # validates, instead of running the solver twice.
            exec_text = case.text
            if ":produce-models" not in exec_text:
                exec_text = "(set-option :produce-models true)\n" + exec_text
            if "(get-model)" not in exec_text:
                exec_text = exec_text.rstrip() + "\n(get-model)\n"
            case_path.write_text(exec_text, encoding="utf-8")

            ours = run_solver(
                [args.solver, "--quiet", "--stats", *args.solver_arg, str(case_path)],
                args.timeout,
                True,
            )
            reference = run_solver([z3_path, str(case_path)], args.timeout, False)

            dolmen_outcome: str | None = None
            if ours.answer == "sat":
                dolmen_outcome = validate_model_with_dolmen(
                    dolmen_path, z3_path, case.text, ours.stdout, args.out, stem, args.dolmen_timeout
                )

            outcome = classify(case, ours, reference, dolmen_outcome, monotone_elimination)
            counts[outcome] = counts.get(outcome, 0) + 1
            writer.writerow(
                {
                    "case": str(case_path),
                    "shape": case.shape,
                    "answer": ours.answer,
                    "reference": reference.answer,
                    "dolmen": dolmen_outcome or "",
                    "outcome": outcome,
                }
            )
            summary.flush()
            os.fsync(summary.fileno())
            if outcome != "PASS":
                shutil.copy2(case_path, failures_dir / f"{outcome.lower()}-{case_path.name}")
            print(f"{case_path}: {outcome} shape={case.shape} answer={ours.answer}", flush=True)

    metadata["status"] = "complete"
    metadata["outcomes"] = counts
    write_metadata(metadata_path, metadata)
    print(f"summary: {summary_path}", flush=True)
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}", flush=True)

    failures = {
        "ACTIVATION_MISSING",
        "CRASH",
        "DOLMEN_ERROR",
        "DOLMEN_MISSING",
        "DOLMEN_TIMEOUT",
        "MISSING",
        "MODEL_ERROR",
        "REFERENCE_DISAGREEMENT",
        "REFERENCE_UNUSABLE",
        "SOLVER_ERROR",
        "UNKNOWN_OUTPUT",
        "UNSUPPORTED_INPUT",
    }
    return 1 if any(counts.get(outcome, 0) for outcome in failures) else 0


if __name__ == "__main__":
    raise SystemExit(main())
