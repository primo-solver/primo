#!/usr/bin/env python3
"""Validate Primo get-model output with Dolmen and limited theory fallbacks."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run Primo with get-model enabled and validate sat QF_UF, QF_LRA, "
            "and supported mixed QF_UFLRA models with Dolmen; Real-constant "
            "models can fall back to Z3 when Dolmen rejects the generated input syntax."
        )
    )
    parser.add_argument("files", nargs="*", type=Path, help="SMT-LIB files to validate")
    parser.add_argument("--manifest", type=Path, help="Newline-separated SMT-LIB file list")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("."),
        help="Root for relative paths read from --manifest",
    )
    parser.add_argument("--shards", type=int, default=1, help="Number of deterministic shards")
    parser.add_argument("--shard-index", type=int, default=0, help="Zero-based shard to process")
    parser.add_argument("--solver", default="./build/primo", help="Primo executable")
    parser.add_argument(
        "--solver-arg",
        action="append",
        default=[],
        help="Extra argument passed to Primo; repeat for multiple arguments",
    )
    parser.add_argument("--dolmen", default="dolmen", help="Dolmen executable")
    parser.add_argument(
        "--z3",
        default=str(Path("contrib/local/bin/z3"))
        if Path("contrib/local/bin/z3").exists()
        else "z3",
        help="Z3 executable used as a limited QF_LRA Real-constant fallback",
    )
    parser.add_argument(
        "--dolmen-arg",
        action="append",
        default=[],
        help="Extra argument passed to Dolmen; repeat for multiple arguments",
    )
    parser.add_argument(
        "--check-model-mode",
        choices=("full", "interleave"),
        help="Optional Dolmen model-checking mode when supported by the installed Dolmen binary",
    )
    parser.add_argument(
        "--require-dolmen-pass",
        action="store_true",
        help="Fail rows that need the limited Z3 fallback instead of passing Dolmen directly",
    )
    parser.add_argument("--timeout", type=float, default=10.0, help="Per-solver timeout in seconds")
    parser.add_argument(
        "--dolmen-timeout",
        type=float,
        default=20.0,
        help="Per-Dolmen timeout in seconds",
    )
    parser.add_argument("--limit", type=int, help="Maximum number of files to process")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("results/qfuf-dolmen-model-validation"),
        help="Output artifact directory",
    )
    return parser.parse_args()


def manifest_files(manifest: Path | None, root: Path) -> list[Path]:
    if manifest is None:
        return []
    files: list[Path] = []
    for line in manifest.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            path = Path(stripped)
            files.append(path if path.is_absolute() else root / path)
    return files


def write_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def model_problem_text(text: str) -> str:
    if ":produce-models" not in text:
        text = "(set-option :produce-models true)\n" + text
    return text


def solver_problem_text(text: str) -> str:
    text = model_problem_text(text)
    if "(get-model" not in text and "( get-model" not in text:
        text = text.rstrip() + "\n(get-model)\n"
    return text


def first_answer(stdout: str) -> str:
    for line in stdout.splitlines():
        stripped = line.strip()
        if stripped in {"sat", "unsat", "unknown"}:
            return stripped
    return ""


def has_solver_error(stdout: str, stderr: str) -> bool:
    return "(error" in stdout or "(error" in stderr


def safe_stem(index: int, path: Path) -> str:
    stem = path.name.replace("/", "_").replace("\\", "_")
    return f"{index:05d}-{stem}"


def extract_real_assignments(model_text: str) -> list[tuple[str, str]]:
    assignments: list[tuple[str, str]] = []
    for line in model_text.splitlines():
        stripped = line.strip()
        prefix = "(define-fun "
        signature = " () Real "
        if not stripped.startswith(prefix) or signature not in stripped or not stripped.endswith(")"):
            continue
        symbol_and_value = stripped[len(prefix) : -1]
        symbol, value = symbol_and_value.split(signature, 1)
        assignments.append((symbol, value.strip()))
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


def validate_real_assignments_with_z3(
    z3_path: str | None,
    original: str,
    assignments: list[tuple[str, str]],
    timeout: float,
) -> tuple[str, int | None, str, str, float]:
    if z3_path is None or not assignments:
        return "unavailable", None, "", "Z3 fallback unavailable\n", 0.0
    validation_input = add_assignments_before_check_sat(
        without_model_commands(original), assignments
    )
    return run_command([z3_path, "-smt2", "-in"], timeout, validation_input)


def run_command(
    command: list[str], timeout: float, input_text: str | None = None
) -> tuple[str, int | None, str, str, float]:
    start = time.monotonic()
    try:
        completed = subprocess.run(
            command,
            check=False,
            text=True,
            input=input_text,
            capture_output=True,
            timeout=timeout,
        )
        elapsed = time.monotonic() - start
        return "done", completed.returncode, completed.stdout, completed.stderr, elapsed
    except subprocess.TimeoutExpired as exc:
        elapsed = time.monotonic() - start
        return "timeout", None, exc.stdout or "", exc.stderr or "", elapsed


def main() -> int:
    args = parse_args()
    if args.shards <= 0:
        print("--shards must be positive", file=sys.stderr)
        return 2
    if args.shard_index < 0 or args.shard_index >= args.shards:
        print("--shard-index must be in [0, --shards)", file=sys.stderr)
        return 2
    dolmen_path = shutil.which(args.dolmen)
    z3_path = shutil.which(args.z3)
    if dolmen_path is None and z3_path is None:
        print(
            f"dolmen executable not found: {args.dolmen}; z3 executable not found: {args.z3}",
            file=sys.stderr,
        )
        return 2
    if args.require_dolmen_pass and dolmen_path is None:
        print(f"required dolmen executable not found: {args.dolmen}", file=sys.stderr)
        return 2

    files = manifest_files(args.manifest, args.root) + args.files
    if args.limit is not None:
        files = files[: args.limit]
    files = [path for index, path in enumerate(files) if index % args.shards == args.shard_index]
    if not files:
        print("no input files provided", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, str]] = []
    failures = 0
    fieldnames = [
        "file",
        "answer",
        "outcome",
        "validator",
        "solver_exit",
        "dolmen_exit",
        "z3_exit",
        "seconds",
        "message",
    ]
    summary_path = args.out / "summary.csv"
    metadata_path = args.out / "metadata.json"
    with summary_path.open("w", encoding="utf-8", newline="") as handle:
        csv.DictWriter(handle, fieldnames=fieldnames).writeheader()
        handle.flush()
        os.fsync(handle.fileno())
    write_json(
        metadata_path,
        {
            "status": "running",
            "count": len(files),
            "shards": args.shards,
            "shard_index": args.shard_index,
            "solver": args.solver,
            "solver_args": args.solver_arg,
            "require_dolmen_pass": args.require_dolmen_pass,
            "dolmen": dolmen_path,
            "z3": z3_path,
        },
    )

    def record(row: dict[str, str]) -> None:
        normalized = {field: row.get(field, "") for field in fieldnames}
        rows.append(normalized)
        with summary_path.open("a", encoding="utf-8", newline="") as handle:
            csv.DictWriter(handle, fieldnames=fieldnames).writerow(normalized)
            handle.flush()
            os.fsync(handle.fileno())
        print(
            f"{normalized['file']}: {normalized['outcome']} "
            f"answer={normalized['answer']} validator={normalized['validator']}",
            flush=True,
        )

    for index, input_path in enumerate(files):
        artifact_stem = safe_stem(index, input_path)
        problem_path = args.out / f"{artifact_stem}.smt2"
        response_path = args.out / f"{artifact_stem}.rsmt2"
        solver_stdout_path = args.out / f"{artifact_stem}.solver.stdout"
        solver_stderr_path = args.out / f"{artifact_stem}.solver.stderr"
        dolmen_stdout_path = args.out / f"{artifact_stem}.dolmen.stdout"
        dolmen_stderr_path = args.out / f"{artifact_stem}.dolmen.stderr"

        try:
            original = input_path.read_text(encoding="utf-8")
        except OSError as exc:
            failures += 1
            record(
                {
                    "file": str(input_path),
                    "answer": "",
                    "outcome": "read-error",
                    "solver_exit": "",
                    "dolmen_exit": "",
                    "seconds": "0.000",
                    "message": str(exc),
                }
            )
            continue

        problem_path.write_text(model_problem_text(original), encoding="utf-8")
        solver_input_path = args.out / f"{artifact_stem}.primo-input.smt2"
        solver_input_path.write_text(solver_problem_text(original), encoding="utf-8")

        solver_status, solver_exit, solver_stdout, solver_stderr, solver_seconds = run_command(
            [args.solver, *args.solver_arg, str(solver_input_path)], args.timeout
        )
        solver_stdout_path.write_text(solver_stdout, encoding="utf-8")
        solver_stderr_path.write_text(solver_stderr, encoding="utf-8")
        answer = first_answer(solver_stdout)

        if solver_status == "timeout":
            record(
                {
                    "file": str(input_path),
                    "answer": "",
                    "outcome": "solver-timeout",
                    "solver_exit": "",
                    "dolmen_exit": "",
                    "seconds": f"{solver_seconds:.3f}",
                    "message": "solver timeout",
                }
            )
            failures += 1
            continue
        if solver_exit != 0:
            record(
                {
                    "file": str(input_path),
                    "answer": answer,
                    "outcome": "solver-error",
                    "solver_exit": str(solver_exit),
                    "dolmen_exit": "",
                    "seconds": f"{solver_seconds:.3f}",
                    "message": "solver exited nonzero",
                }
            )
            failures += 1
            continue
        if answer == "unsat":
            record(
                {
                    "file": str(input_path),
                    "answer": answer,
                    "outcome": "not-sat",
                    "solver_exit": str(solver_exit),
                    "dolmen_exit": "",
                    "seconds": f"{solver_seconds:.3f}",
                    "message": "no model expected",
                }
            )
            continue
        if answer != "sat":
            record(
                {
                    "file": str(input_path),
                    "answer": answer,
                    "outcome": "solver-unknown" if answer == "unknown" else "solver-output-error",
                    "solver_exit": str(solver_exit),
                    "seconds": f"{solver_seconds:.3f}",
                    "message": "model production did not return sat or unsat",
                }
            )
            failures += 1
            continue

        response_path.write_text(solver_stdout, encoding="utf-8")
        z3_exit: int | None = None
        validator = "dolmen"
        if dolmen_path is not None:
            assignments = extract_real_assignments(solver_stdout)
            dolmen_command = [
                dolmen_path,
                "--check-model=true",
                "-r",
                str(response_path),
                *args.dolmen_arg,
                str(problem_path),
            ]
            if args.check_model_mode is not None:
                dolmen_command.insert(2, f"--check-model-mode={args.check_model_mode}")
            dolmen_status, dolmen_exit, dolmen_stdout, dolmen_stderr, validation_seconds = (
                run_command(dolmen_command, args.dolmen_timeout)
            )
            dolmen_stdout_path.write_text(dolmen_stdout, encoding="utf-8")
            dolmen_stderr_path.write_text(dolmen_stderr, encoding="utf-8")
            if dolmen_status == "timeout":
                outcome = "dolmen-timeout"
                failures += 1
            elif dolmen_exit == 0:
                outcome = "pass"
            else:
                validator = "z3-fallback"
                z3_status, z3_exit, z3_stdout, z3_stderr, z3_seconds = (
                    validate_real_assignments_with_z3(
                        z3_path, original, assignments, args.dolmen_timeout
                    )
                )
                validation_seconds += z3_seconds
                if (
                    z3_status == "done"
                    and z3_exit == 0
                    and not has_solver_error(z3_stdout, z3_stderr)
                    and first_answer(z3_stdout) == "sat"
                ):
                    dolmen_stdout_path.write_text(
                        dolmen_stdout
                        + "\n--- Z3 fallback stdout ---\n"
                        + z3_stdout,
                        encoding="utf-8",
                    )
                    dolmen_stderr_path.write_text(
                        dolmen_stderr
                        + "\n--- Z3 fallback stderr ---\n"
                        + z3_stderr,
                        encoding="utf-8",
                    )
                    if args.require_dolmen_pass:
                        outcome = "fallback-pass"
                        failures += 1
                    else:
                        outcome = "pass"
                else:
                    outcome = "model-error"
                    failures += 1
        else:
            validator = "z3-only"
            assignments = extract_real_assignments(solver_stdout)
            dolmen_stdout_path.write_text("", encoding="utf-8")
            dolmen_stderr_path.write_text("Dolmen unavailable; used Z3 fallback\n", encoding="utf-8")
            z3_status, z3_exit, dolmen_stdout, dolmen_stderr, validation_seconds = (
                validate_real_assignments_with_z3(
                    z3_path, original, assignments, args.dolmen_timeout
                )
            )
            dolmen_stdout_path.write_text(dolmen_stdout, encoding="utf-8")
            dolmen_stderr_path.write_text(dolmen_stderr, encoding="utf-8")
            z3_answer = first_answer(dolmen_stdout)
            if z3_status == "timeout":
                outcome = "z3-timeout"
                failures += 1
            elif (
                z3_status == "done"
                and z3_exit == 0
                and not has_solver_error(dolmen_stdout, dolmen_stderr)
                and z3_answer == "sat"
                and assignments
            ):
                outcome = "pass"
            elif z3_answer == "unsat":
                outcome = "model-error"
                failures += 1
            else:
                outcome = "validation-unavailable"
                failures += 1
        record(
            {
                "file": str(input_path),
                "answer": answer,
                "outcome": outcome,
                "validator": validator,
                "solver_exit": str(solver_exit),
                "dolmen_exit": "" if dolmen_exit is None else str(dolmen_exit),
                "z3_exit": "" if z3_exit is None else str(z3_exit),
                "seconds": f"{solver_seconds + validation_seconds:.3f}",
                "message": "",
            }
        )

    counts: dict[str, int] = {}
    validators: dict[str, int] = {}
    for row in rows:
        counts[row["outcome"]] = counts.get(row["outcome"], 0) + 1
        if row["validator"]:
            validators[row["validator"]] = validators.get(row["validator"], 0) + 1
    write_json(
        metadata_path,
        {
            "status": "complete",
            "count": len(files),
            "completed": len(rows),
            "shards": args.shards,
            "shard_index": args.shard_index,
            "solver": args.solver,
            "solver_args": args.solver_arg,
            "require_dolmen_pass": args.require_dolmen_pass,
            "dolmen": dolmen_path,
            "z3": z3_path,
            "outcomes": counts,
            "validators": validators,
        },
    )
    print(f"wrote {summary_path}")
    print(" ".join(f"{key}={counts[key]}" for key in sorted(counts)))
    print(" ".join(f"validator.{key}={validators[key]}" for key in sorted(validators)))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
