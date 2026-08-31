#!/usr/bin/env python3
"""Run legal production tactic profiles through Primo's public CLI."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ANSWERS = {"sat", "unsat", "unknown"}
LOGICS = {"QF_UF", "QF_LRA", "QF_UFLRA", "QF_LIA"}
FAILURE_OUTCOMES = {
    "answer-mismatch",
    "crash",
    "invalid-statistics",
    "missing-statistic",
    "reference-disagreement",
    "reference-mismatch",
    "timeout",
    "unknown-output",
}


@dataclass(frozen=True)
class Profile:
    identifier: str
    args: tuple[str, ...]
    logics: frozenset[str]
    required_stats: tuple[str, ...]
    required_stats_by_logic: dict[str, tuple[str, ...]]
    required_observed_stats_by_logic: dict[str, tuple[str, ...]]


@dataclass(frozen=True)
class Case:
    path: Path
    logic: str
    expected: str | None


@dataclass(frozen=True)
class CommandResult:
    status: str
    returncode: int | None
    stdout: str
    stderr: str
    seconds: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("benchmarks/tactic-composition-matrix.json"),
    )
    parser.add_argument(
        "--case-list",
        type=Path,
        help="Newline-delimited cases replacing the manifest's deterministic corpus",
    )
    parser.add_argument("--logic", choices=sorted(LOGICS))
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--refs", nargs="+", default=["z3", "cvc5"])
    parser.add_argument("--dolmen", default="dolmen")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--dolmen-timeout", type=float, default=10.0)
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--profiles", nargs="+")
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--allow-inconclusive",
        action="store_true",
        help="Report unknown/timeouts without failing; correctness failures still fail",
    )
    parser.add_argument(
        "--skip-model-validation",
        action="store_true",
        help="Skip Dolmen; intended only for unit tests of this harness",
    )
    return parser.parse_args()


def _string_list(value: Any, field: str) -> tuple[str, ...]:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError(f"{field} must be a list of strings")
    return tuple(value)


def load_manifest(path: Path, root: Path) -> tuple[list[Profile], list[Case]]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise TypeError("manifest root must be an object")
    raw_profiles = raw.get("profiles")
    raw_cases = raw.get("cases")
    if not isinstance(raw_profiles, list) or not isinstance(raw_cases, list):
        raise TypeError("manifest must contain profile and case lists")

    profiles: list[Profile] = []
    seen_profiles: set[str] = set()
    for entry in raw_profiles:
        if not isinstance(entry, dict) or not isinstance(entry.get("id"), str):
            raise TypeError("every profile needs a string id")
        identifier = entry["id"]
        if identifier in seen_profiles:
            raise ValueError(f"duplicate profile id: {identifier}")
        seen_profiles.add(identifier)
        logics = frozenset(_string_list(entry.get("logics"), f"{identifier}.logics"))
        if not logics or not logics <= LOGICS:
            raise ValueError(
                f"{identifier}.logics contains an unknown or empty logic set"
            )
        by_logic_raw = entry.get("required_stats_by_logic", {})
        if not isinstance(by_logic_raw, dict) or not set(by_logic_raw) <= LOGICS:
            raise ValueError(f"{identifier}.required_stats_by_logic is invalid")
        by_logic = {
            logic: _string_list(value, f"{identifier}.required_stats_by_logic.{logic}")
            for logic, value in by_logic_raw.items()
        }
        observed_by_logic_raw = entry.get("required_observed_stats_by_logic", {})
        if (
            not isinstance(observed_by_logic_raw, dict)
            or not set(observed_by_logic_raw) <= LOGICS
        ):
            raise ValueError(
                f"{identifier}.required_observed_stats_by_logic is invalid"
            )
        observed_by_logic = {
            logic: _string_list(
                value, f"{identifier}.required_observed_stats_by_logic.{logic}"
            )
            for logic, value in observed_by_logic_raw.items()
        }
        if not set(observed_by_logic) <= logics:
            raise ValueError(
                f"{identifier}.required_observed_stats_by_logic names an inapplicable logic"
            )
        profiles.append(
            Profile(
                identifier=identifier,
                args=_string_list(entry.get("args"), f"{identifier}.args"),
                logics=logics,
                required_stats=_string_list(
                    entry.get("required_stats", []), f"{identifier}.required_stats"
                ),
                required_stats_by_logic=by_logic,
                required_observed_stats_by_logic=observed_by_logic,
            )
        )

    cases: list[Case] = []
    for entry in raw_cases:
        if not isinstance(entry, dict):
            raise TypeError("every case must be an object")
        logic = entry.get("logic")
        expected = entry.get("expected")
        relative_path = entry.get("path")
        if logic not in LOGICS or expected not in {"sat", "unsat"}:
            raise ValueError(
                "every deterministic case needs a supported logic and result"
            )
        if not isinstance(relative_path, str):
            raise TypeError("every case needs a string path")
        case_path = Path(relative_path)
        case_path = case_path if case_path.is_absolute() else root / case_path
        cases.append(Case(case_path, logic, expected))
    return profiles, cases


def load_case_list(path: Path, root: Path, logic: str) -> list[Case]:
    cases: list[Case] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        case_path = Path(stripped)
        cases.append(
            Case(
                case_path if case_path.is_absolute() else root / case_path, logic, None
            )
        )
    return cases


def executable(command: str) -> str | None:
    first = shlex.split(command)[0]
    if "/" in first:
        path = Path(first)
        return (
            str(path.resolve()) if path.is_file() and os.access(path, os.X_OK) else None
        )
    return shutil.which(first)


def run_command(command: list[str], timeout: float) -> CommandResult:
    started = time.monotonic()
    try:
        process = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        return CommandResult(
            "timeout",
            None,
            error.stdout or "",
            error.stderr or "",
            time.monotonic() - started,
        )
    return CommandResult(
        "done",
        process.returncode,
        process.stdout,
        process.stderr,
        time.monotonic() - started,
    )


def answer(stdout: str) -> str:
    for line in stdout.splitlines():
        stripped = line.strip()
        if stripped in ANSWERS:
            return stripped
    return ""


def statistics(stderr: str) -> dict[str, int]:
    parsed: dict[str, int] = {}
    for line in stderr.splitlines():
        fields = line.split()
        if len(fields) != 2 or not fields[0].startswith(":"):
            continue
        if fields[0].startswith(":time."):
            continue
        try:
            parsed[fields[0]] = int(fields[1])
        except ValueError as error:
            raise ValueError(f"invalid statistic line: {line}") from error
    return parsed


def select_profiles(
    all_profiles: list[Profile], selected: list[str] | None
) -> list[Profile]:
    if not selected:
        return all_profiles
    requested = set(selected)
    known = {profile.identifier for profile in all_profiles}
    missing = sorted(requested - known)
    if missing:
        raise ValueError(f"unknown profiles: {', '.join(missing)}")
    return [profile for profile in all_profiles if profile.identifier in requested]


def reference_answers(
    cases: list[Case], references: list[list[str]], timeout: float
) -> tuple[dict[Path, tuple[str, ...]], list[str]]:
    results: dict[Path, tuple[str, ...]] = {}
    failures: list[str] = []
    for case in cases:
        answers: list[str] = []
        for reference in references:
            result = run_command([*reference, str(case.path)], timeout)
            current = answer(result.stdout)
            if result.status == "timeout":
                answers.append("timeout")
            elif result.returncode != 0:
                answers.append("crash")
            else:
                answers.append(current or "unknown-output")
        concrete = {current for current in answers if current in {"sat", "unsat"}}
        if any(current in {"crash", "unknown-output"} for current in answers):
            failures.append(
                f"{case.path}: unusable reference result: {' '.join(answers)}"
            )
        elif len(concrete) > 1:
            failures.append(f"{case.path}: reference disagreement: {' '.join(answers)}")
        elif case.expected is not None and concrete and concrete != {case.expected}:
            failures.append(
                f"{case.path}: expected {case.expected}, references returned {' '.join(answers)}"
            )
        results[case.path] = tuple(answers)
    return results, failures


def classify(
    result: CommandResult,
    actual: str,
    expected: str | None,
    references: tuple[str, ...],
    parsed_stats: dict[str, int],
    required_stats: tuple[str, ...],
) -> str:
    if result.status == "timeout":
        return "timeout"
    if result.returncode != 0:
        return "crash"
    if not actual:
        return "unknown-output"
    concrete = {value for value in references if value in {"sat", "unsat"}}
    if len(concrete) > 1:
        return "reference-disagreement"
    target = expected or (next(iter(concrete)) if concrete else None)
    if target is not None and actual in {"sat", "unsat"} and actual != target:
        return "answer-mismatch"
    if actual == "unknown" or target is None:
        return "inconclusive"
    if any(parsed_stats.get(name, 0) == 0 for name in required_stats):
        return "missing-statistic"
    return "pass"


def write_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def git_revision(root: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        check=False,
        text=True,
        capture_output=True,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def run_model_validation(
    profile: Profile,
    cases: list[Case],
    args: argparse.Namespace,
    solver_path: str,
) -> tuple[int, int]:
    profile_out = args.out / profile.identifier / "model-validation"
    profile_out.mkdir(parents=True, exist_ok=True)
    case_manifest = profile_out / "cases.txt"
    case_manifest.write_text(
        "".join(f"{case.path.resolve()}\n" for case in cases), encoding="utf-8"
    )
    validator = Path(__file__).with_name("validate_model.py")
    command = [
        sys.executable,
        str(validator),
        "--manifest",
        str(case_manifest),
        "--root",
        ".",
        "--solver",
        solver_path,
        "--dolmen",
        args.dolmen,
        "--timeout",
        str(args.timeout),
        "--dolmen-timeout",
        str(args.dolmen_timeout),
        "--dolmen-arg=--strict=false",
        "--require-dolmen-pass",
        "--out",
        str(profile_out),
    ]
    for solver_arg in ("--quiet", *profile.args):
        command.append(f"--solver-arg={solver_arg}")
    returncode = subprocess.run(command, check=False).returncode
    if returncode == 0:
        return 0, 0
    if not args.allow_inconclusive:
        return 1, 0
    summary = profile_out / "summary.csv"
    if not summary.is_file():
        return 1, 0
    with summary.open(newline="", encoding="utf-8") as handle:
        outcomes = [row["outcome"] for row in csv.DictReader(handle)]
    accepted = {"pass", "not-sat", "solver-timeout", "solver-unknown"}
    if any(outcome not in accepted for outcome in outcomes):
        return 1, 0
    return 0, sum(
        outcome in {"solver-timeout", "solver-unknown"} for outcome in outcomes
    )


def main() -> int:
    args = parse_args()
    if args.repeat < 1 or args.shards < 1 or not 0 <= args.shard_index < args.shards:
        print("repeat/shard values are out of range", file=sys.stderr)
        return 2
    if args.case_list is not None and args.logic is None:
        print("--logic is required with --case-list", file=sys.stderr)
        return 2

    try:
        profiles, cases = load_manifest(args.manifest, args.root)
        profiles = select_profiles(profiles, args.profiles)
        if args.case_list is not None:
            cases = load_case_list(args.case_list, args.root, args.logic)
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"invalid composition manifest: {error}", file=sys.stderr)
        return 2
    cases = [
        case
        for index, case in enumerate(cases)
        if index % args.shards == args.shard_index
    ]
    if not cases:
        print("the selected shard has no cases", file=sys.stderr)
        return 2
    missing_cases = [str(case.path) for case in cases if not case.path.is_file()]
    if missing_cases:
        print(f"missing cases: {', '.join(missing_cases)}", file=sys.stderr)
        return 2

    solver_path = executable(args.solver)
    if solver_path is None:
        print(f"solver is unavailable: {args.solver}", file=sys.stderr)
        return 2
    references = [shlex.split(reference) for reference in args.refs]
    missing_refs = [
        reference[0] for reference in references if executable(reference[0]) is None
    ]
    if missing_refs:
        print(
            f"reference solvers are unavailable: {', '.join(missing_refs)}",
            file=sys.stderr,
        )
        return 2
    if not args.skip_model_validation and executable(args.dolmen) is None:
        print(f"Dolmen is unavailable: {args.dolmen}", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    solver_version = run_command([solver_path, "--version"], args.timeout)
    reference_results, reference_failures = reference_answers(
        cases, references, args.timeout
    )
    for failure in reference_failures:
        print(failure, file=sys.stderr)

    fieldnames = [
        "profile",
        "logic",
        "file",
        "repeat",
        "answer",
        "references",
        "outcome",
        "returncode",
        "seconds",
        "statistics",
    ]
    summary_path = args.out / "summary.csv"
    failures = len(reference_failures)
    inconclusive = 0
    rows = 0
    seen_statistics: dict[tuple[str, Path], dict[str, int]] = {}
    with summary_path.open("w", encoding="utf-8", newline="") as summary:
        writer = csv.DictWriter(summary, fieldnames=fieldnames)
        writer.writeheader()
        summary.flush()
        os.fsync(summary.fileno())
        for profile in profiles:
            eligible = [case for case in cases if case.logic in profile.logics]
            if not eligible:
                continue
            profile_out = args.out / profile.identifier / "runs"
            profile_out.mkdir(parents=True, exist_ok=True)
            observed_by_logic: dict[str, set[str]] = {}
            for case_index, case in enumerate(eligible):
                required = profile.required_stats + profile.required_stats_by_logic.get(
                    case.logic, ()
                )
                for repeat in range(args.repeat):
                    result = run_command(
                        [
                            solver_path,
                            "--quiet",
                            "--stats",
                            *profile.args,
                            str(case.path),
                        ],
                        args.timeout,
                    )
                    stem = f"{case_index:05d}-{case.path.name}.repeat-{repeat}"
                    (profile_out / f"{stem}.stdout").write_text(
                        result.stdout, encoding="utf-8"
                    )
                    (profile_out / f"{stem}.stderr").write_text(
                        result.stderr, encoding="utf-8"
                    )
                    try:
                        parsed_stats = statistics(result.stderr)
                    except ValueError:
                        parsed_stats = {}
                        outcome = "invalid-statistics"
                    else:
                        outcome = classify(
                            result,
                            answer(result.stdout),
                            case.expected,
                            reference_results[case.path],
                            parsed_stats,
                            required,
                        )
                    key = (profile.identifier, case.path)
                    observed = observed_by_logic.setdefault(case.logic, set())
                    observed.update(
                        name for name, value in parsed_stats.items() if value > 0
                    )
                    if repeat == 0:
                        seen_statistics[key] = parsed_stats
                    elif outcome == "pass" and parsed_stats != seen_statistics[key]:
                        outcome = "invalid-statistics"
                    row = {
                        "profile": profile.identifier,
                        "logic": case.logic,
                        "file": str(case.path),
                        "repeat": str(repeat),
                        "answer": answer(result.stdout),
                        "references": " ".join(reference_results[case.path]),
                        "outcome": outcome,
                        "returncode": ""
                        if result.returncode is None
                        else str(result.returncode),
                        "seconds": f"{result.seconds:.6f}",
                        "statistics": json.dumps(parsed_stats, sort_keys=True),
                    }
                    writer.writerow(row)
                    summary.flush()
                    os.fsync(summary.fileno())
                    rows += 1
                    if outcome == "timeout" and args.allow_inconclusive:
                        inconclusive += 1
                    elif outcome in FAILURE_OUTCOMES:
                        failures += 1
                    elif outcome == "inconclusive":
                        inconclusive += 1
                    print(
                        f"{profile.identifier} {case.logic} {case.path.name} "
                        f"repeat={repeat}: {outcome}",
                        flush=True,
                    )

            missing_observations: dict[str, list[str]] = {}
            for logic in {case.logic for case in eligible}:
                missing = sorted(
                    set(profile.required_observed_stats_by_logic.get(logic, ()))
                    - observed_by_logic.get(logic, set())
                )
                if missing:
                    missing_observations[logic] = missing
                    failures += len(missing)
            write_json(
                args.out / profile.identifier / "profile-check.json",
                {
                    "required_observed_stats_by_logic": profile.required_observed_stats_by_logic,
                    "observed_stats_by_logic": {
                        logic: sorted(values)
                        for logic, values in observed_by_logic.items()
                    },
                    "missing_observations": missing_observations,
                },
            )
            for logic, missing in missing_observations.items():
                print(
                    f"{profile.identifier} {logic}: required activity was not observed: "
                    f"{', '.join(missing)}",
                    file=sys.stderr,
                )

            if not args.skip_model_validation:
                model_failures, model_inconclusive = run_model_validation(
                    profile, eligible, args, solver_path
                )
                failures += model_failures
                inconclusive += model_inconclusive

    status = (
        "failed"
        if failures or (inconclusive and not args.allow_inconclusive)
        else "passed"
    )
    write_json(
        args.out / "metadata.json",
        {
            "status": status,
            "manifest": str(args.manifest),
            "commit": git_revision(args.root),
            "solver": solver_path,
            "solver_version": solver_version.stdout.strip(),
            "references": args.refs,
            "profiles": [profile.identifier for profile in profiles],
            "case_count": len(cases),
            "rows": rows,
            "repeat": args.repeat,
            "shards": args.shards,
            "shard_index": args.shard_index,
            "failures": failures,
            "inconclusive": inconclusive,
            "model_validation": not args.skip_model_validation,
            "dolmen_required": not args.skip_model_validation,
        },
    )
    print(f"summary: {summary_path}")
    print(
        f"status={status} rows={rows} failures={failures} inconclusive={inconclusive}"
    )
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
