#!/usr/bin/env python3
"""Record benchmark provenance and independently replay its successful SAT rows."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
INCOMPLETE = {
    "solver-timeout",
    "solver-unknown",
    "dolmen-timeout",
    "input-unsupported",
    "input-nonconforming",
    "validation-unavailable",
    "fallback-pass",
}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def executable_identity(command: str) -> dict[str, str]:
    found = shutil.which(command)
    if found is None:
        raise ValueError(f"missing executable: {command}")
    path = Path(found).resolve()
    proc = subprocess.run(
        [str(path), "--version"], capture_output=True, text=True, timeout=15, check=True
    )
    return {"path": str(path), "sha256": digest(path), "version": proc.stdout.strip()}


def selected_sat_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [
        row
        for row in rows
        if row["status"] == "ok"
        and row["answer"] == "sat"
        and row["exit_code"] == "0"
        and row["timeout"] == "false"
    ]


def coverage(expected: list[str], rows: list[dict[str, str]]) -> dict[str, object]:
    expected_set = set(expected)
    observed = [row["file"] for row in rows]
    counts = Counter(row["outcome"] for row in rows)
    malformed = len(set(observed)) != len(observed) or bool(
        set(observed) - expected_set
    )
    missing = sorted(expected_set - set(observed))
    invalid_pass = any(
        row["outcome"] == "pass"
        and (row.get("validator") != "dolmen" or row.get("answer") != "sat")
        for row in rows
    )
    hard_failure = (
        malformed or invalid_pass or bool(set(counts) - INCOMPLETE - {"pass"})
    )
    status = (
        "failed"
        if hard_failure
        else ("incomplete" if missing or set(counts) - {"pass"} else "complete")
    )
    return {
        "status": status,
        "expected_sat": len(expected),
        "completed": len(rows),
        "dolmen_pass": counts["pass"] if not invalid_pass else 0,
        "outcomes": dict(counts),
        "missing": missing,
        "malformed_rows": malformed,
    }


def benchmark(args: argparse.Namespace) -> int:
    root = args.root.resolve()
    paths = sorted(path for path in root.rglob("*.smt2") if path.is_file())
    paths = paths[args.shard_index :: args.shards]
    identity = executable_identity(args.solver)
    if (
        args.expected_git
        and f"git: {args.expected_git}" not in identity["version"].splitlines()
    ):
        raise ValueError("binary revision does not match --expected-git")
    args.out.mkdir(parents=True, exist_ok=True)
    provenance = args.out / "provenance.json"
    if provenance.exists():
        raise ValueError("benchmark output already exists; use a fresh run directory")
    inputs = {str(path.relative_to(root)): digest(path) for path in paths}
    metadata = {
        "schema_version": 1,
        "status": "running",
        "run_id": args.run_id,
        "root": str(root),
        "solver": identity,
        "solver_args": args.solver_arg,
        "timeout": args.timeout,
        "shards": args.shards,
        "shard_index": args.shard_index,
        "inputs": inputs,
    }
    write_json(provenance, metadata)
    command = [
        sys.executable,
        str(SCRIPTS / "bench_qf_uf_full.py"),
        "--repo",
        str(root),
        "--root",
        ".",
        "--solver",
        identity["path"],
        *[f"--solver-arg={arg}" for arg in args.solver_arg],
        "--shards",
        str(args.shards),
        "--shard-index",
        str(args.shard_index),
        "--timeout",
        str(args.timeout),
        "--out",
        str(args.out / "answers.csv"),
        "--summary",
        str(args.out / "answer-summary.csv"),
        "--log-dir",
        str(args.out / "solver-output"),
    ]
    result = subprocess.run(command, check=False)
    unchanged = all(digest(root / name) == value for name, value in inputs.items())
    unchanged &= digest(Path(identity["path"])) == identity["sha256"]
    metadata["status"] = (
        "complete" if result.returncode == 0 and unchanged else "failed"
    )
    metadata["command"] = command
    metadata["inputs_unchanged"] = unchanged
    write_json(provenance, metadata)
    return 0 if metadata["status"] == "complete" else 1


def replay(args: argparse.Namespace) -> int:
    parent = args.out
    source = parent / "provenance.json"
    metadata = json.loads(source.read_text())
    if metadata["status"] != "complete":
        raise ValueError(
            "parent benchmark is incomplete; do not validate an unnoticed prefix"
        )
    solver = metadata["solver"]
    if executable_identity(solver["path"]) != solver:
        raise ValueError("benchmark binary was replaced")
    root = Path(metadata["root"])
    with (parent / "answers.csv").open() as stream:
        answers = list(csv.DictReader(stream))
    names = [row["file"] for row in answers]
    if len(names) != len(set(names)) or set(names) != set(metadata["inputs"]):
        raise ValueError("benchmark rows do not cover the recorded input manifest")
    selected = selected_sat_rows(answers)
    for row in selected:
        name = row["file"]
        if digest(root / name) != metadata["inputs"][name]:
            raise ValueError(f"benchmark input was replaced: {name}")
    output = parent / args.attempt
    output.mkdir(exist_ok=True)
    identity = {
        "parent_run": metadata["run_id"],
        "parent_provenance_sha256": digest(source),
        "parent_answers_sha256": digest(parent / "answers.csv"),
        "validator_script_sha256": digest(
            SCRIPTS / "validate_qfuf_models_with_dolmen.py"
        ),
        "dolmen": executable_identity(args.dolmen),
        "dolmen_args": args.dolmen_arg,
        "solver_timeout": args.model_timeout,
        "dolmen_timeout": args.dolmen_timeout,
    }
    config_path = output / "replay-provenance.json"
    if config_path.exists():
        if json.loads(config_path.read_text()) != identity:
            raise ValueError(
                "replay identity changed; use a separate validation directory"
            )
        result_path = output / "coverage.json"
        if result_path.exists():
            previous = json.loads(result_path.read_text())
            if previous["status"] == "complete":
                for relative, checksum in previous["artifacts"].items():
                    artifact = output / relative
                    if (digest(artifact) if artifact.exists() else None) != checksum:
                        raise ValueError(
                            "completed replay artifacts changed or are missing"
                        )
                print("matching complete replay already exists")
                return 0
        raise ValueError(
            "partial replay exists; preserve it and use --attempt for a new attempt"
        )
    write_json(config_path, identity)
    manifest = output / "sat-manifest.txt"
    expected = [str(root / row["file"]) for row in selected]
    if any("\n" in name or "\r" in name for name in expected):
        raise ValueError(
            "newline in benchmark path is not supported by the manifest format"
        )
    manifest.write_text("".join(name + "\n" for name in expected))
    identity["manifest_sha256"] = digest(manifest)
    # Manifest identity is also in the coverage artifact; keep configuration stable for resume.
    command = [
        sys.executable,
        str(SCRIPTS / "validate_model.py"),
        "--solver",
        solver["path"],
        *[f"--solver-arg={arg}" for arg in metadata["solver_args"]],
        "--solver-arg=--check-model",
        "--expect-sat",
        "--require-dolmen-pass",
        "--dolmen",
        args.dolmen,
        *[f"--dolmen-arg={arg}" for arg in args.dolmen_arg],
        "--timeout",
        str(args.model_timeout),
        "--dolmen-timeout",
        str(args.dolmen_timeout),
        "--manifest",
        str(manifest),
        "--out",
        str(output / "models"),
    ]
    code = subprocess.run(command, check=False).returncode if expected else 0
    summary = output / "models" / "summary.csv"
    rows = []
    if summary.exists():
        with summary.open() as stream:
            rows = list(csv.DictReader(stream))
    report = coverage(expected, rows)
    report["artifacts"] = {
        "sat-manifest.txt": digest(manifest),
        "models/summary.csv": digest(summary) if summary.exists() else None,
    }
    report.update({"provenance": identity, "command": command, "validator_exit": code})
    stable = digest(Path(solver["path"])) == solver["sha256"] and all(
        digest(root / row["file"]) == metadata["inputs"][row["file"]]
        for row in selected
    )
    report["inputs_and_binary_unchanged"] = stable
    if not stable:
        report["status"] = "failed"
    if code not in (0, 1):
        report["status"] = "failed"
    write_json(output / "coverage.json", report)
    print(json.dumps(report, sort_keys=True))
    return {"complete": 0, "incomplete": 2, "failed": 1}[str(report["status"])]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("benchmark", "replay"))
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--solver", default="./build-slurm-rel/primo")
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--expected-git")
    parser.add_argument("--run-id", default="manual")
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--model-timeout", type=float, default=90)
    parser.add_argument("--dolmen-timeout", type=float, default=30)
    parser.add_argument("--dolmen", default="dolmen")
    parser.add_argument("--dolmen-arg", action="append", default=[])
    parser.add_argument(
        "--attempt",
        default="model-validation",
        help="Separate artifact directory name for a fresh replay attempt",
    )
    args = parser.parse_args()
    args.out = args.out.resolve()
    if args.shards < 1 or not 0 <= args.shard_index < args.shards:
        parser.error("invalid shard configuration")
    if args.attempt in {"", ".", ".."} or Path(args.attempt).name != args.attempt:
        parser.error("--attempt must be a simple directory name")
    try:
        return benchmark(args) if args.phase == "benchmark" else replay(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"benchmark/model replay error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
