#!/usr/bin/env python3
"""Collect bounded LRA structure traces; timings are diagnostic, not benchmarks."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from analyze_lra_bound_probe import analyze
from bench_qf_uf_full import parse_smtlib_status


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(command, timeout, stem):
    try:
        result = subprocess.run(
            command, capture_output=True, text=True, timeout=timeout, check=False
        )
        status = "ok" if result.returncode == 0 else "error"
        stdout, stderr, code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as error:
        status, code = "timeout", None
        stdout = (
            (error.stdout or b"").decode()
            if isinstance(error.stdout, bytes)
            else error.stdout or ""
        )
        stderr = (
            (error.stderr or b"").decode()
            if isinstance(error.stderr, bytes)
            else error.stderr or ""
        )
    stem.with_suffix(".stdout").write_text(stdout)
    stem.with_suffix(".stderr").write_text(stderr)
    answers = [
        line.strip()
        for line in stdout.splitlines()
        if line.strip() in ("sat", "unsat", "unknown")
    ]
    answer = answers[-1] if answers else None
    return {"command": command, "status": status, "answer": answer, "exit_code": code}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--dolmen", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--limit", type=int, default=4000)
    parser.add_argument("--constructed", action="store_true")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    solver = str(args.solver.resolve())
    reference = str(args.reference.resolve())
    profiles = {
        "default": [],
        "rows": ["--lra-row-propagation", "--lra-bidirectional-row-propagation"],
    }
    common = ["--quiet"]
    if args.constructed:
        common += [
            "--preprocessing-mode",
            "off",
            "--no-solved-constant-substitution",
            "--no-gaussian-elimination",
        ]
    records = []
    paths = [
        args.root / line.split("#", 1)[0].strip()
        for line in args.manifest.read_text().splitlines()
        if line.split("#", 1)[0].strip()
    ]
    metadata = {
        "solver_sha256": digest(args.solver),
        "solver_version": subprocess.check_output([solver, "--version"], text=True),
        "reference_sha256": digest(args.reference),
        "manifest_sha256": digest(args.manifest),
        "timeout": args.timeout,
        "limit": args.limit,
        "profiles": profiles,
        "common": common,
        "records": records,
    }
    for index, path in enumerate(paths):
        case_dir = args.out / f"{index:03d}"
        case_dir.mkdir()
        ref = invoke(
            [reference, str(path.resolve())], args.timeout, case_dir / "reference"
        )
        expected = parse_smtlib_status(path)
        if ref["status"] == "error" or (
            ref["status"] == "ok" and ref["answer"] is None
        ):
            (case_dir / "reference-failure.json").write_text(json.dumps(ref, indent=2))
            raise SystemExit(f"reference error triage required: {path}")
        for profile, flags in profiles.items():
            trace = case_dir / f"{profile}.jsonl"
            command = [
                solver,
                *common,
                *flags,
                "--lra-bound-probe",
                str(trace.resolve()),
                "--lra-bound-probe-limit",
                str(args.limit),
                str(path.resolve()),
            ]
            run = invoke(command, args.timeout, case_dir / profile)
            if run["status"] == "ok" and run["answer"] is None:
                run["status"] = "error"
            record = {
                "file": str(path),
                "expected": expected,
                "input_sha256": digest(path),
                "profile": profile,
                "reference": ref,
                **run,
                "trace": analyze(trace) if trace.exists() else None,
            }
            records.append(record)
            (args.out / "summary.json").write_text(
                json.dumps(metadata, indent=2) + "\n"
            )
            known_mismatch = expected in ("sat", "unsat") and any(
                result["status"] == "ok"
                and result["answer"] in ("sat", "unsat")
                and result["answer"] != expected
                for result in (run, ref)
            )
            if (
                run["status"] == "error"
                or known_mismatch
                or (
                    run["status"] == ref["status"] == "ok"
                    and run["answer"] in ("sat", "unsat")
                    and ref["answer"] in ("sat", "unsat")
                    and run["answer"] != ref["answer"]
                )
            ):
                raise SystemExit(f"answer/error triage required: {path} {profile}")
    # Correctness replay follows trace collection. Original trace files remain immutable.
    for profile, flags in profiles.items():
        selected = [
            r
            for r in records
            if r["profile"] == profile and r["status"] == "ok" and r["answer"] == "sat"
        ]
        manifest = args.out / f"{profile}-sat.txt"
        manifest.write_text(
            "".join(str(Path(r["file"]).resolve()) + "\n" for r in selected)
        )
        if not selected:
            continue
        command = [
            sys.executable,
            str(Path(__file__).with_name("validate_model.py")),
            "--solver",
            solver,
            "--expect-sat",
            "--require-dolmen-pass",
            "--dolmen",
            str(args.dolmen.resolve()),
            "--dolmen-size",
            "2G",
            "--dolmen-arg=--strict=false",
            "--timeout",
            "30",
            "--dolmen-timeout",
            "15",
            "--manifest",
            str(manifest),
            "--out",
            str(args.out / f"{profile}-models"),
        ]
        for flag in [
            *common,
            *flags,
            "--check-model",
            "--lra-bound-probe",
            str((args.out / f"{profile}-model-replay.jsonl").resolve()),
            "--lra-bound-probe-limit",
            str(args.limit),
        ]:
            command.append("--solver-arg=" + flag)
        record = invoke(command, 900, args.out / f"{profile}-models-driver")
        metadata.setdefault("model_replays", {})[profile] = record
        (args.out / "summary.json").write_text(json.dumps(metadata, indent=2) + "\n")
        if record["status"] != "ok":
            raise SystemExit("model replay failed or incomplete: " + profile)
    print(
        json.dumps(
            {"inputs": len(paths), "profiles": len(profiles), "records": len(records)}
        )
    )


if __name__ == "__main__":
    main()
