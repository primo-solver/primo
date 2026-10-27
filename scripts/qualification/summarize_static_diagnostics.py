#!/usr/bin/env python3
"""Audit the fixed-source diagnostic qualification without executing solvers."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
from collections import Counter
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path):
    return json.loads(path.read_text())


def csv_rows(path: Path) -> list[dict]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def models(path: Path, expected: int, options: list[str]) -> dict:
    rows = csv_rows(path / "summary.csv")
    metadata = load(path / "metadata.json")
    assert len(rows) == expected == metadata["completed"] == metadata["count"]
    assert metadata["status"] == "complete" and metadata["require_dolmen_pass"]
    assert metadata["solver_args"] == options, metadata["solver_args"]
    assert all(
        row["outcome"] == "pass"
        and row["validator"] == "dolmen"
        and row["solver_exit"] == "0"
        and row["dolmen_exit"] == "0"
        for row in rows
    )
    return {
        "models": len(rows),
        "solver_args": options,
        "summary_sha256": digest(path / "summary.csv"),
        "metadata_sha256": digest(path / "metadata.json"),
    }


def summarize(root: Path, repo: Path) -> dict:
    jobs = dict(
        line.split("=", 1) for line in (root / "jobs.txt").read_text().splitlines()
    )
    candidate = (root / "candidate").read_text().strip()
    generated = root / "remote" / jobs["generated"]
    qualification = root / "remote/diagnostic-qualification"
    routes = {}
    binary_hashes = set()
    for first, logic in ((0, "QF_UF"), (12, "QF_UFLRA")):
        cases = model_count = 0
        summaries = []
        for slot in range(first, first + 4):
            shard = generated / f"shard-{slot}"
            assert (shard / "exit-code").read_text().strip() == "0"
            assert f"git: {candidate[:7]}\n" in (shard / "version.txt").read_text()
            binary_hashes.add((shard / "executables.sha256").read_text().split()[0])
            rows = csv_rows(shard / "gate/summary.csv")
            assert len(rows) == 250 and all(
                row["outcome"] == "PASS"
                and row["ours"] in ("sat", "unsat")
                and row["ours_code"] == "0"
                for row in rows
            )
            sat = sum(row["ours"] == "sat" for row in rows)
            model_dir = shard / ("gate/model-validation" if first == 0 else "models")
            checked = models(model_dir, sat, ["--quiet"])
            cases += len(rows)
            model_count += sat
            summaries.append(
                {
                    "slot": slot,
                    "cases": len(rows),
                    **checked,
                    "answers_sha256": digest(shard / "gate/summary.csv"),
                }
            )
        routes[logic] = {
            "cases": cases,
            "pass": cases,
            "models": model_count,
            "shards": summaries,
        }
    sessions = queries = sat_models = 0
    session_reports = []
    for slot in range(24, 28):
        shard = generated / f"shard-{slot}"
        assert (shard / "exit-code").read_text().strip() == "0"
        binary_hashes.add((shard / "executables.sha256").read_text().split()[0])
        summary = load(shard / "gate/summary.json")
        run = load(shard / "gate/run.json")
        assert f"git: {candidate[:7]}\n" in run["version"]
        assert summary["sessions"] == summary["pass"] == 250 and not summary["failures"]
        observed = observed_queries = observed_models = 0
        for session in sorted((shard / "gate").glob("session-*")):
            result = load(session / "result.json")
            assert result["outcome"] == "pass"
            observed += 1
            for query in result["queries"]:
                assert query["outcome"] == "pass"
                assert (
                    query["answer"] in ("sat", "unsat")
                    and query["answer"] == query["expected"]
                )
                observed_queries += 1
                directory = session / f"query-{query['query']:04d}"
                assert digest(directory / "context.smt2") == query["context_sha256"]
                if query["answer"] == "sat":
                    assert (
                        "dolmen_command" in query
                        and (directory / "model.rsmt2").stat().st_size
                    )
                    assert (directory / "dolmen.stdout").is_file()
                    observed_models += 1
        assert (observed, observed_queries, observed_models) == (
            250,
            summary["queries"],
            summary["sat_models"],
        )
        sessions += observed
        queries += observed_queries
        sat_models += observed_models
        session_reports.append(
            {
                "slot": slot,
                **summary,
                "summary_sha256": digest(shard / "gate/summary.json"),
            }
        )
    routes["incremental_QF_UF"] = {
        "cases": sessions,
        "pass": sessions,
        "queries": queries,
        "models": sat_models,
        "shards": session_reports,
    }
    online = {}
    online_references = []
    accepted = qualification / "online-euf-attested"
    for corpus, expected in (
        ("general", 250),
        ("finite-qf-uf", 25),
        ("finite-qf-uflra", 25),
    ):
        cases = model_count = 0
        shards = []
        options = ["--qf-uflra-online-euf"]
        if corpus == "finite-qf-uf":
            options.append("--preprocessing-mode=strong")
        for task in range(4):
            directory = accepted / f"task-{task}"
            assert (directory / "exit-code").read_text().strip() == "0"
            before = (directory / "binary-before.sha256").read_text()
            assert before == (directory / "binary-after.sha256").read_text()
            assert before.split()[0] in binary_hashes
            references = {
                Path(line.split(maxsplit=1)[1]).name: line.split()[0]
                for line in (directory / "references.sha256").read_text().splitlines()
            }
            online_references.append(references)
            assert f"git: {candidate[:7]}\n" in (directory / "version.txt").read_text()
            rows = csv_rows(directory / corpus / "summary.csv")
            assert len(rows) == expected and all(
                row["outcome"] == "PASS" for row in rows
            )
            if corpus != "general":
                assert (
                    load(directory / corpus / "metadata.json")["solver_args"] == options
                )
            sat = sum(row["ours"] == "sat" for row in rows)
            checked = models(
                directory / "model-validation" / corpus, sat, ["--quiet", *options]
            )
            cases += len(rows)
            model_count += sat
            shards.append(
                {
                    "task": task,
                    "cases": len(rows),
                    **checked,
                    "answers_sha256": digest(directory / corpus / "summary.csv"),
                }
            )
        online[corpus] = {
            "cases": cases,
            "pass": cases,
            "models": model_count,
            "shards": shards,
        }
    for phase in ("checks", "asan"):
        directory = qualification / phase
        assert (directory / "exit-code").read_text().strip() == "0"
        assert "100% tests passed out of 45" in (directory / "stdout.log").read_text()
    assert "Ran 284 tests" in (qualification / "checks/stderr.log").read_text()
    baseline_job = (root / "baseline-job.txt").read_text().strip()
    baseline = load(qualification / f"baseline-{baseline_job}/report.json")
    assert baseline["test_exit"] in (-6, 134)
    assert baseline["source_sha256"] == digest(repo / "tests/unit/euf_solver_tests.cpp")
    cli_job = (root / "asan-cli-job.txt").read_text().strip()
    cli = qualification / f"asan-cli-{cli_job}"
    assert (cli / "exit-code").read_text().strip() == "0"
    assert (
        not (cli / "help.stderr").read_text()
        and not (cli / "version.stderr").read_text()
    )
    static = load(root / "final-tidy/rechecked-summary.json")
    assert (
        static["status"] == "complete-with-findings" and static["tool_exit_code"] == 0
    )
    assert static["analyzed_translation_units"] == 132 and not static["changed_inputs"]
    assert static["unused_include_gate"]["status"] == "passed"
    manifest = load(root / "final-tidy/database/manifest.json")
    assert all(
        digest(repo / name) == value
        for name, value in manifest["source_sha256"].items()
    )
    subprocess.run(
        ["git", "diff", "--exit-code", candidate, "--", *manifest["source_sha256"]],
        cwd=repo,
        capture_output=True,
        check=True,
    )
    assert len(binary_hashes) == 1
    assert all(refs == online_references[0] for refs in online_references)
    return {
        "id": "2026-10-27-static-diagnostic-repair",
        "date": "2026-10-27",
        "candidate_git": candidate,
        "binary_sha256": binary_hashes.pop(),
        "jobs": {
            **jobs,
            "build": "258327",
            "baseline": baseline_job,
            "asan_cli": cli_job,
            "online_accepted": (root / "online-attested-job.txt").read_text().strip(),
        },
        "reference_sha256": online_references[0],
        "routes": routes,
        "online_euf": online,
        "total_cases_or_sessions": sum(row["cases"] for row in routes.values())
        + sum(row["cases"] for row in online.values()),
        "total_direct_dolmen_models": sum(row["models"] for row in routes.values())
        + sum(row["models"] for row in online.values()),
        "native_tests": {
            "release": 45,
            "asan_lsan": 45,
            "python_at_native_snapshot": 284,
            "doc_examples": 12,
        },
        "baseline_regression": {
            key: baseline[key]
            for key in (
                "source_sha256",
                "baseline_library_sha256",
                "test_exit",
                "scope",
            )
        },
        "asan_cli": {"exit": 0, "stderr": "empty", "paths": ["--help", "--version"]},
        "fresh_static": {
            key: static[key]
            for key in (
                "status",
                "analyzed_translation_units",
                "unique_diagnostics",
                "counts",
                "tool_exit_code",
            )
        },
        "triage_counts": dict(
            Counter(
                row["disposition"]
                for row in load(
                    repo / "docs/coverage/local-static-triage-20261027.json"
                )["annotations"]
            )
        ),
        "native_source_sha256": manifest["source_sha256"],
        "scope": "Correctness/resource regression qualification, not a timing or default-promotion claim.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = summarize(args.artifacts, args.repo)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {
                key: report[key]
                for key in (
                    "total_cases_or_sessions",
                    "total_direct_dolmen_models",
                    "native_tests",
                )
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
