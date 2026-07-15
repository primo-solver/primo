#!/usr/bin/env python3
"""irace target runner for Primo QF_UF profile tuning.

The runner prints a single numeric cost for irace.  Correct solved runs cost
their elapsed wall time.  Timeouts and ordinary errors cost PAR2.  Wrong answers
cost a much larger penalty so a fast unsound configuration cannot win a race.
"""

from __future__ import annotations

import os
import shlex
import subprocess
import sys
import time
from pathlib import Path


ANSWERS = {"sat", "unsat", "unknown", "unsupported"}
CHECKABLE = {"sat", "unsat"}


def split_runner_args(argv: list[str]) -> tuple[Path, dict[str, str]]:
    """Accept irace's target-runner prefix plus switched parameter values."""

    args = list(argv)
    while args and not args[0].endswith(".smt2"):
        args.pop(0)
    if not args:
        raise SystemExit("target_runner.py: missing SMT-LIB instance path")
    instance = Path(args.pop(0))
    # Some irace versions pass a time bound immediately after the instance.
    if args and not args[0].startswith("--"):
        args.pop(0)

    params: dict[str, str] = {}
    index = 0
    while index < len(args):
        key = args[index]
        if not key.startswith("--"):
            index += 1
            continue
        if index + 1 >= len(args) or args[index + 1].startswith("--"):
            params[key.removeprefix("--")] = "1"
            index += 1
        else:
            params[key.removeprefix("--")] = args[index + 1]
            index += 2
    return instance, params


def parse_status(path: Path) -> str:
    try:
        for raw_line in path.read_text(errors="replace").splitlines():
            line = raw_line.strip()
            if line.startswith("(set-info :status "):
                status = line.removeprefix("(set-info :status ").rstrip(")").strip()
                return status if status in CHECKABLE else ""
    except OSError:
        return ""
    return ""


def add_flag(args: list[str], enabled: bool, flag: str) -> None:
    if enabled:
        args.append(flag)


def add_budget(args: list[str], flag: str, value: str) -> None:
    if value == "default":
        return
    args.append(f"{flag}={value}")


def solver_args(params: dict[str, str]) -> list[str]:
    args: list[str] = ["--quiet"]
    preprocess = params.get("preprocess", "auto")
    if preprocess != "default":
        args.append(f"--qf-uf-preprocess={preprocess}")

    args.append(f"--preprocessing-passes={params.get('passes', '4')}")
    args.append(f"--preprocessing-adaptive-extra-passes={params.get('adaptive_extra', '4')}")
    args.append(f"--preprocessing-adaptive-shrink-percent={params.get('adaptive_shrink', '10')}")

    if params.get("symmetry", "on") == "off":
        args.append("--no-uf-symmetry-breaking")
    if params.get("eq_bridge", "on") == "off":
        args.append("--no-eq-bridge")
    add_flag(args, params.get("ground_congruence", "off") == "on", "--ground-congruence")
    add_flag(args, params.get("ground_unit_substitution", "off") == "on",
             "--ground-unit-substitution")
    add_flag(args, params.get("guarded_ground_unit_substitution", "off") == "on",
             "--guarded-ground-unit-substitution")

    args.append(f"--euf-engine={params.get('euf_engine', 'incremental')}")
    args.append(f"--euf-prop={params.get('euf_prop', 'auto')}")
    args.append(f"--pure-theory-filter={params.get('pure_filter', 'auto')}")
    add_budget(args, "--euf-prop-budget", params.get("prop_budget", "auto"))
    add_budget(args, "--euf-prop-delivery", params.get("prop_delivery", "1000"))
    args.append(f"--prop-interval={params.get('prop_interval', '32')}")
    args.append(f"--prop-assign-threshold={params.get('prop_threshold', '0.25')}")

    cadical = params.get("cadical_profile", "default")
    if cadical == "incremental":
        for option in [
            "ilb=2",
            "sweep=0",
            "congruence=0",
            "backbone=0",
            "condition=0",
            "elim=0",
            "vivify=0",
            "probe=0",
            "subsume=0",
            "transred=0",
            "walk=0",
        ]:
            args.extend(["--cadical-option", option])
    return args


def completed_text(output: str | bytes | None) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode(errors="replace")
    return output


def main() -> int:
    instance, params = split_runner_args(sys.argv[1:])
    solver = shlex.split(os.environ.get("PRIMO_TUNE_SOLVER", "./build/primo"))
    timeout = float(os.environ.get("PRIMO_TUNE_TIMEOUT", "30"))
    wrong_penalty = float(os.environ.get("PRIMO_TUNE_WRONG_PENALTY", str(1000 * timeout)))
    expected = parse_status(instance)
    command = [*solver, *solver_args(params), str(instance)]
    start = time.perf_counter()
    timed_out = False
    try:
        proc = subprocess.run(
            command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
        stdout = proc.stdout
        stderr = proc.stderr
        exit_code = proc.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        stdout = completed_text(exc.stdout)
        stderr = completed_text(exc.stderr)
        exit_code = 124
    elapsed = time.perf_counter() - start
    first_line = stdout.strip().splitlines()[0] if stdout.strip() else ""
    answer = first_line if first_line in ANSWERS else "error"
    wrong = bool(expected) and answer in CHECKABLE and answer != expected
    if wrong:
        cost = wrong_penalty
    elif timed_out or (exit_code != 0 and answer == "error"):
        cost = 2.0 * timeout
    elif answer in CHECKABLE:
        cost = elapsed
    else:
        cost = 2.0 * timeout

    log_path = os.environ.get("PRIMO_TUNE_LOG")
    if log_path:
        with Path(log_path).open("a") as handle:
            handle.write(
                f"{instance},{answer},{expected},{elapsed:.6f},{cost:.6f},{exit_code},"
                f"{' '.join(shlex.quote(arg) for arg in command)}\n"
            )
            if stderr and wrong:
                handle.write(f"# stderr: {' '.join(stderr.splitlines()[:4])}\n")
    print(f"{cost:.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
