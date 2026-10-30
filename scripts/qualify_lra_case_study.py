#!/usr/bin/env python3
"""Join frozen inputs to answers and exact models per docs/papers/lra-call-ablation.md."""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import Counter
from pathlib import Path

from prepare_lra_call_replay import check_conjunction
from prepare_lra_case_study import conjunction_id, parent_name, sha256, warm_pivots
from validate_qfuf_models_with_dolmen import model_problem_text


def csv_rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def load_native(directory, expected_count):
    shards = [
        json.loads(p.read_text())
        for p in sorted(directory.glob("shard-*/summary.json"))
    ]
    assert shards and len(shards) == shards[0]["shards"]
    rows = {}
    for shard in shards:
        assert shard["complete"]
        for key in (
            "solver_sha256",
            "version",
            "reference_sha256",
            "manifest_sha256",
            "expected_statuses_sha256",
            "timeout_seconds",
            "solver_args",
            "workflow_sha256",
        ):
            assert shard[key] == shards[0][key], key
        for row in shard["records"]:
            assert not row["wrong"] and row["file"] not in rows
            rows[row["file"]] = row
    assert len(rows) == expected_count
    return shards[0], rows


def model_checks(directory, corpus, expected, native=False):
    checked = {}
    summaries = (
        [directory / "summary.csv"]
        if (directory / "summary.csv").exists()
        else sorted(directory.glob("shard-*/summary.csv"))
    )
    for summary in summaries:
        metadata = json.loads(summary.with_name("metadata.json").read_text())
        assert (
            metadata["status"] == "complete"
            and metadata["expect_sat"]
            and metadata["require_dolmen_pass"]
        )
        if native:
            assert "--check-model" in metadata["solver_args"]
        for index, row in enumerate(csv_rows(summary)):
            name = "cubes/" + Path(row["file"]).name
            assert name not in checked and name in expected
            assert (
                row["answer"],
                row["outcome"],
                row["validator"],
                row["solver_exit"],
                row["dolmen_exit"],
            ) == ("sat", "pass", "dolmen", "0", "0")
            adapted = summary.parent / f"{index:05d}-{Path(name).name}.smt2"
            assert adapted.read_text() == model_problem_text(
                (corpus / name).read_text()
            )
            checked[name] = {
                "validator": "dolmen",
                "outcome": "pass",
                "adapted_sha256": sha256(adapted),
            }
    assert set(checked) == expected, (
        f"incomplete SAT model coverage: {len(checked)}/{len(expected)}"
    )
    return checked


def decisive(run):
    if run["status"] == "ok" and run["answer"] in ("sat", "unsat"):
        assert int(run["exit_code"]) == 0
        return run["answer"]
    return None


def quantiles(values):
    values = sorted(values)
    return {
        label: values[max(0, math.ceil(len(values) * fraction) - 1)] if values else None
        for label, fraction in [
            ("min", 0),
            ("median", 0.5),
            ("p90", 0.9),
            ("p99", 0.99),
            ("max", 1),
        ]
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--identities", type=Path, required=True)
    args = parser.parse_args()
    selection = json.loads((args.corpus / "selection.json").read_text())
    files = (args.corpus / "manifest.txt").read_text().splitlines()
    assert len(files) == len(set(files)) == selection["selected"] == 1000
    expected = json.loads((args.corpus / "expected-statuses.json").read_text())
    assert set(expected) == set(files)
    identities = set()
    for row in selection["records"]:
        path = args.corpus / row["file"]
        assert sha256(path) == row["sha256"] == path.stem
        assert check_conjunction(path.read_text()) == row["syntax"]
        identity = conjunction_id(path.read_text())
        assert identity == row["conjunction_id"] and identity not in identities
        identities.add(identity)
    provenance, fresh = load_native(args.results / "fresh", len(files))
    assert provenance["manifest_sha256"] == sha256(args.corpus / "manifest.txt")
    assert provenance["expected_statuses_sha256"] == sha256(
        args.corpus / "expected-statuses.json"
    )
    assert provenance["solver_sha256"] == selection["binary_sha256"]
    identities = json.loads(args.identities.read_text())
    assert identities["primo"]["sha256"] == provenance["solver_sha256"]
    assert identities["z3"]["sha256"] == provenance["reference_sha256"]
    answers = {
        "primo": {n: r["native"] for n, r in fresh.items()},
        "z3": {n: r["reference"] for n, r in fresh.items()},
    }
    references = {}
    for label in ["yices-smt2", "opensmt"]:
        rows = {}
        identity = None
        for path in sorted(
            (args.results / "references" / label).glob("shard-*/provenance.json")
        ):
            meta = json.loads(path.read_text())
            assert meta["status"] == "complete" and meta["inputs_unchanged"]
            if identity is None:
                identity = meta["solver"]
            assert identity == meta["solver"]
            for name, checksum in meta["inputs"].items():
                assert checksum == sha256(args.corpus / name)
            for row in csv_rows(path.with_name("answers.csv")):
                assert row["file"] not in rows and row["wrong"] == "false"
                rows[row["file"]] = row
        assert set(rows) == set(files)
        answers[label] = rows
        references[label] = {k: identity[k] for k in ("sha256", "version")}
        assert references[label]["sha256"] == identities[label]["sha256"]
        references[label] = identities[label]
    references["z3"] = identities["z3"]
    proofs = {}
    for label, runs in answers.items():
        selected_sat = {name for name, run in runs.items() if decisive(run) == "sat"}
        if label == "primo":
            directory = args.results / "models"
        elif label == "z3":
            directory = args.results / "z3-models"
        else:
            directory = args.results / "references" / label
        if label in ("yices-smt2", "opensmt"):
            checks = {}
            for shard in directory.glob("shard-*"):
                model_dir = shard / "models"
                if not model_dir.exists():
                    continue
                subset = {
                    n
                    for n in selected_sat
                    if n
                    in json.loads((shard / "provenance.json").read_text())["inputs"]
                }
                for n, proof in model_checks(model_dir, args.corpus, subset).items():
                    assert n not in checks
                    checks[n] = proof
            assert set(checks) == selected_sat
            proofs[label] = checks
        else:
            proofs[label] = model_checks(
                directory, args.corpus, selected_sat, native=(label == "primo")
            )
    label_directory = args.results / "label-models"
    label_names = {
        "cubes/" + Path(row["file"]).name
        for path in label_directory.glob("shard-*/summary.csv")
        for row in csv_rows(path)
    }
    assert all(expected[name] == "sat" for name in label_names)
    label_proofs = model_checks(label_directory, args.corpus, label_names, native=True)
    records = []
    verified = Counter()
    outcomes = {}
    single_checks = []
    single_times = []
    for label, runs in answers.items():
        assert all(r["status"] in ("ok", "timeout") for r in runs.values())
        outcomes[label] = dict(
            Counter(decisive(r) or r["status"] for r in runs.values())
        )
    for origin in selection["records"]:
        name = origin["file"]
        native = fresh[name]
        assert native["input_sha256"] == origin["sha256"]
        verdicts = {a for label in answers if (a := decisive(answers[label][name]))}
        assert len(verdicts) <= 1, "reference disagreement"
        known = expected[name]
        assert not verdicts or known == "unknown" or verdicts == {known}
        if known == "sat":
            assert name in label_proofs or any(
                name in checks for checks in proofs.values()
            ), "SAT label has no direct model check"
            verified["sat"] += 1
        elif known == "unsat":
            assert any(
                decisive(answers[label][name]) == "unsat"
                for label in ("z3", "yices-smt2", "opensmt")
            ), "UNSAT label lacks independent reference agreement"
            verified["unsat"] += 1
        else:
            assert not verdicts, (
                "new verdict requires classification before freezing truth metadata"
            )
            verified["unknown"] += 1
        analysis = native["analysis"]
        if analysis["coverage"] == "complete" and analysis["calls"] == 1:
            single_checks.append(analysis["total_pivots"])
            single_times.append(analysis["total_check_ns"])
        records.append(
            {
                "file": name,
                "sha256": origin["sha256"],
                "conjunction_id": origin["conjunction_id"],
                "expected": known,
                "counting_parent": parent_name(origin),
                "warm_max_pivots": warm_pivots(origin),
                "retained": origin["retained"],
                "fresh_coverage": analysis["coverage"],
                "fresh_completed_checks": analysis["calls"],
                "fresh_completed_pivots": analysis.get("total_pivots", 0),
                "fresh_completed_check_ns": analysis.get("total_check_ns", 0),
                "runs": {
                    label: {
                        k: answers[label][name][k]
                        for k in ("status", "answer", "seconds", "exit_code")
                    }
                    for label in answers
                },
                "models": {label: checks.get(name) for label, checks in proofs.items()},
                "additional_label_model": label_proofs.get(name),
            }
        )
    result = {
        "status": "qualified-known-labels-with-explicit-unknown-research-candidate",
        "inputs": len(files),
        "expected_statuses": dict(verified),
        "native_version": provenance["version"],
        "native_binary_sha256": provenance["solver_sha256"],
        "manifest_sha256": provenance["manifest_sha256"],
        "expected_statuses_sha256": provenance["expected_statuses_sha256"],
        "timeout_seconds": provenance["timeout_seconds"],
        "references": references,
        "validator": identities["dolmen"],
        "native_solver_args": provenance["solver_args"],
        "outcomes": outcomes,
        "direct_dolmen_model_passes": {
            label: len(checks) for label, checks in proofs.items()
        },
        "additional_label_model_passes": len(label_proofs),
        "native_check_counts": dict(
            Counter(r["fresh_completed_checks"] for r in records)
        ),
        "fresh_coverage": dict(Counter(r["fresh_coverage"] for r in records)),
        "complete_single_check_inputs": len(single_checks),
        "fresh_single_check_pivots": quantiles(single_checks),
        "fresh_single_check_ns": quantiles(single_times),
        "fresh_single_check_bins": dict(
            Counter(
                "0-31"
                if p < 32
                else "32-127"
                if p < 128
                else "128-1023"
                if p < 1024
                else "1024+"
                for p in single_checks
            )
        ),
        "counting_families": dict(
            Counter(parent_name(r).split("/")[0] for r in selection["records"])
        ),
        "counting_parents": len({parent_name(r) for r in selection["records"]}),
        "max_selected_per_parent": max(
            Counter(parent_name(r) for r in selection["records"]).values()
        ),
        "retained_inputs": sum(r["retained"] for r in records),
        "limitations": [
            "Conjunction identity ignores comments/status/whitespace, not logical equivalence.",
            "One source corpus and first qualifying exports; quotas do not establish representativeness.",
            "No warm basis, assignment, reason graph or original Boolean context is replayed.",
            "Whole-process timeouts and unfinished checks are censored, not completed durations.",
            "No algorithmic winner, speedup or default promotion is claimed.",
        ],
        "records": records,
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "records"}, indent=2))


if __name__ == "__main__":
    main()
