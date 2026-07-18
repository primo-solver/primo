#!/usr/bin/env python3
"""SMAC tuner for focused QF_UF reference gaps.

The campaign is intentionally narrow.  The objective is PAR2 on a small
training manifest, with wrong answers penalized much more heavily than
timeouts.  After SMAC finishes, the incumbent and current defaults are
validated on a separate holdout manifest.
"""

from __future__ import annotations

import argparse
import csv
import json
import shlex
import subprocess
import time
from pathlib import Path
from typing import Any


ANSWERS = {"sat", "unsat", "unknown", "unsupported"}
CHECKABLE = {"sat", "unsat"}

INCREMENTAL_CADICAL_OPTIONS = [
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
]


def read_manifest(repo: Path, manifest: Path) -> list[Path]:
    files: list[Path] = []
    for raw_line in manifest.read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        path = Path(line)
        files.append(path if path.is_absolute() else repo / path)
    return files


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


def completed_text(output: str | bytes | None) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode(errors="replace")
    return output


def config_value(config: Any, key: str, default: Any) -> Any:
    try:
        if key in config:
            return config[key]
    except TypeError:
        pass
    if isinstance(config, dict):
        return config.get(key, default)
    return default


def config_dict(config: Any) -> dict[str, Any]:
    if isinstance(config, dict):
        return dict(config)
    if hasattr(config, "get_dictionary"):
        return dict(config.get_dictionary())
    return {key: config[key] for key in config}


def solver_args(config: Any) -> list[str]:
    args = ["--quiet"]

    preprocess = config_value(config, "preprocess", "default")
    if preprocess != "default":
        args.append(f"--qf-uf-preprocess={preprocess}")

    args.append(f"--preprocessing-passes={config_value(config, 'passes', 4)}")
    args.append(
        "--preprocessing-adaptive-extra-passes="
        f"{config_value(config, 'adaptive_extra', 4)}"
    )
    args.append(
        "--preprocessing-adaptive-shrink-percent="
        f"{config_value(config, 'adaptive_shrink', 10)}"
    )

    if config_value(config, "symmetry", "on") == "off":
        args.append("--no-uf-symmetry-breaking")
    if config_value(config, "value_precedence", "on") == "off":
        args.append("--no-finite-domain-value-precedence")
    equality_definitions = config_value(config, "equality_definitions", "default")
    if equality_definitions == "on":
        args.append("--finite-domain-equality-definitions")
    elif equality_definitions == "off":
        args.append("--no-finite-domain-equality-definitions")
    if config_value(config, "eq_bridge", "on") == "off":
        args.append("--no-eq-bridge")
    if config_value(config, "solved_constants", "on") == "off":
        args.append("--no-solved-constant-substitution")
    if config_value(config, "ground_congruence", "off") == "on":
        args.append("--ground-congruence")
    if config_value(config, "ground_unit_substitution", "off") == "on":
        args.append("--ground-unit-substitution")
    if config_value(config, "guarded_ground_unit_substitution", "off") == "on":
        args.append("--guarded-ground-unit-substitution")

    args.append(f"--euf-engine={config_value(config, 'euf_engine', 'incremental')}")
    args.append(f"--euf-prop={config_value(config, 'euf_prop', 'auto')}")
    args.append(f"--pure-theory-filter={config_value(config, 'pure_filter', 'auto')}")
    args.append(f"--theory-phase={config_value(config, 'theory_phase', 'negative')}")
    prop_budget = config_value(config, "prop_budget", "auto")
    if prop_budget != "auto":
        args.append(f"--euf-prop-budget={prop_budget}")
    prop_delivery = config_value(config, "prop_delivery", "1000")
    if prop_delivery != "default":
        args.append(f"--euf-prop-delivery={prop_delivery}")
    args.append(f"--prop-interval={config_value(config, 'prop_interval', '32')}")
    args.append(
        f"--prop-assign-threshold={config_value(config, 'prop_threshold', '0.25')}"
    )

    if config_value(config, "cadical_profile", "default") == "incremental":
        for option in INCREMENTAL_CADICAL_OPTIONS:
            args.extend(["--cadical-option", option])

    return args


def run_solver(
    solver: list[str], config: Any, path: Path, timeout: float
) -> dict[str, Any]:
    command = [*solver, *solver_args(config), str(path)]
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
        exit_code = proc.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        stdout = completed_text(exc.stdout)
        exit_code = 124
    seconds = time.perf_counter() - start
    first_line = stdout.strip().splitlines()[0] if stdout.strip() else ""
    answer = first_line if first_line in ANSWERS else "error"
    expected = parse_status(path)
    wrong = bool(expected) and answer in CHECKABLE and answer != expected
    if wrong:
        cost = 1000.0 * timeout
        status = "wrong"
    elif timed_out or (exit_code != 0 and answer == "error"):
        cost = 2.0 * timeout
        status = "timeout" if timed_out else "error"
    elif answer in CHECKABLE:
        cost = seconds
        status = "ok"
    else:
        cost = 2.0 * timeout
        status = "unknown"
    return {
        "file": str(path),
        "status": status,
        "answer": answer,
        "expected": expected,
        "wrong": str(wrong).lower(),
        "seconds": f"{seconds:.6f}",
        "cost": f"{cost:.6f}",
        "exit_code": exit_code,
        "command": " ".join(shlex.quote(part) for part in command),
    }


def append_rows(path: Path, fieldnames: list[str], rows: list[dict[str, Any]]) -> None:
    exists = path.exists()
    with path.open("a", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        if not exists:
            writer.writeheader()
        writer.writerows(rows)


def make_configspace(seed: int):
    from ConfigSpace import ConfigurationSpace
    from ConfigSpace.hyperparameters import (
        CategoricalHyperparameter,
        UniformIntegerHyperparameter,
    )

    cs = ConfigurationSpace(seed=seed)
    cs.add_hyperparameters(
        [
            CategoricalHyperparameter(
                "preprocess",
                ["default", "auto", "basic", "strong"],
                default_value="default",
            ),
            UniformIntegerHyperparameter("passes", 2, 8, default_value=4),
            UniformIntegerHyperparameter("adaptive_extra", 0, 8, default_value=4),
            UniformIntegerHyperparameter("adaptive_shrink", 0, 30, default_value=10),
            CategoricalHyperparameter("symmetry", ["on", "off"], default_value="on"),
            CategoricalHyperparameter(
                "value_precedence", ["on", "off"], default_value="on"
            ),
            CategoricalHyperparameter(
                "equality_definitions", ["default", "on", "off"], default_value="default"
            ),
            CategoricalHyperparameter("eq_bridge", ["on", "off"], default_value="on"),
            CategoricalHyperparameter(
                "solved_constants", ["on", "off"], default_value="on"
            ),
            CategoricalHyperparameter(
                "ground_congruence", ["off", "on"], default_value="off"
            ),
            CategoricalHyperparameter(
                "ground_unit_substitution", ["off", "on"], default_value="off"
            ),
            CategoricalHyperparameter(
                "guarded_ground_unit_substitution", ["off", "on"], default_value="off"
            ),
            CategoricalHyperparameter(
                "euf_engine", ["incremental", "lazy"], default_value="incremental"
            ),
            CategoricalHyperparameter(
                "euf_prop", ["auto", "conflict", "online", "off"], default_value="auto"
            ),
            CategoricalHyperparameter(
                "pure_filter", ["auto", "on", "off"], default_value="auto"
            ),
            CategoricalHyperparameter(
                "theory_phase",
                ["negative", "positive", "none", "polarity"],
                default_value="negative",
            ),
            CategoricalHyperparameter(
                "prop_budget",
                ["auto", "500", "1000", "3000", "10000", "unlimited"],
                default_value="auto",
            ),
            CategoricalHyperparameter(
                "prop_delivery",
                ["default", "250", "1000", "3000", "10000", "unlimited"],
                default_value="1000",
            ),
            CategoricalHyperparameter(
                "prop_interval", ["8", "16", "32", "64", "128"], default_value="32"
            ),
            CategoricalHyperparameter(
                "prop_threshold",
                ["0.10", "0.25", "0.50", "0.75", "1.0"],
                default_value="0.25",
            ),
            CategoricalHyperparameter(
                "cadical_profile", ["default", "incremental"], default_value="default"
            ),
        ]
    )
    return cs


def validate(
    label: str,
    solver: list[str],
    config: Any,
    files: list[Path],
    timeout: float,
    out_dir: Path,
) -> float:
    rows: list[dict[str, Any]] = []
    total = 0.0
    for path in files:
        row = run_solver(solver, config, path, timeout)
        row["kind"] = label
        row["config_json"] = json.dumps(config_dict(config), sort_keys=True)
        total += float(row["cost"])
        rows.append(row)
    append_rows(
        out_dir / "validation-runs.csv",
        [
            "kind",
            "file",
            "status",
            "answer",
            "expected",
            "wrong",
            "seconds",
            "cost",
            "exit_code",
            "config_json",
            "command",
        ],
        rows,
    )
    return total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument(
        "--train", type=Path, default=Path("benchmarks/qfuf-smac-gap-train.txt")
    )
    parser.add_argument(
        "--validate", type=Path, default=Path("benchmarks/qfuf-smac-gap-validate.txt")
    )
    parser.add_argument("--out-dir", type=Path, default=Path("results/smac/qfuf-gap"))
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--validation-timeout", type=float, default=120.0)
    parser.add_argument("--trials", type=int, default=80)
    parser.add_argument("--seed", type=int, default=20260716)
    args = parser.parse_args()

    repo = args.repo.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    train_files = read_manifest(repo, repo / args.train)
    validation_files = read_manifest(repo, repo / args.validate)
    solver = shlex.split(args.solver)

    try:
        from smac import HyperparameterOptimizationFacade, Scenario
    except ImportError as exc:
        raise SystemExit(
            "SMAC is not installed; run `python -m pip install smac`."
        ) from exc

    cs = make_configspace(args.seed)
    run_fields = [
        "trial",
        "file",
        "status",
        "answer",
        "expected",
        "wrong",
        "seconds",
        "cost",
        "exit_code",
        "config_json",
        "command",
    ]
    trial_counter = {"value": 0}

    def objective(config: Any, seed: int = 0) -> float:
        del seed
        trial_counter["value"] += 1
        trial = trial_counter["value"]
        rows: list[dict[str, Any]] = []
        total = 0.0
        solved = 0
        for path in train_files:
            row = run_solver(solver, config, path, args.timeout)
            row["trial"] = trial
            row["config_json"] = json.dumps(config_dict(config), sort_keys=True)
            total += float(row["cost"])
            if row["status"] == "ok" and row["answer"] in CHECKABLE:
                solved += 1
            rows.append(row)
        append_rows(out_dir / "target-runs.csv", run_fields, rows)
        summary_row = {
            "trial": trial,
            "cost": f"{total:.6f}",
            "solved": solved,
            "total": len(train_files),
            "config_json": json.dumps(config_dict(config), sort_keys=True),
            "args": " ".join(shlex.quote(part) for part in solver_args(config)),
        }
        append_rows(
            out_dir / "trial-summary.csv",
            ["trial", "cost", "solved", "total", "config_json", "args"],
            [summary_row],
        )
        return total

    scenario = Scenario(
        cs,
        deterministic=True,
        n_trials=args.trials,
        output_directory=out_dir / "smac-output",
    )
    smac = HyperparameterOptimizationFacade(scenario, objective, overwrite=True)
    incumbent = smac.optimize()
    incumbent_dict = config_dict(incumbent)
    (out_dir / "incumbent.json").write_text(
        json.dumps(incumbent_dict, indent=2, sort_keys=True)
    )

    default_config = cs.get_default_configuration()
    default_cost = validate(
        "default",
        solver,
        default_config,
        validation_files,
        args.validation_timeout,
        out_dir,
    )
    incumbent_cost = validate(
        "incumbent",
        solver,
        incumbent,
        validation_files,
        args.validation_timeout,
        out_dir,
    )
    with (out_dir / "result.json").open("w") as handle:
        json.dump(
            {
                "default_validation_cost": default_cost,
                "incumbent_validation_cost": incumbent_cost,
                "incumbent": incumbent_dict,
            },
            handle,
            indent=2,
            sort_keys=True,
        )
    print(
        json.dumps(
            {"default": default_cost, "incumbent": incumbent_cost}, sort_keys=True
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
