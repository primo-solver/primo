#!/usr/bin/env python3
"""Classify retained unknown-status captures using exact independent model joins."""

import argparse
import csv
import hashlib
import json
from pathlib import Path

from prepare_lra_call_replay import check_conjunction
from validate_qfuf_models_with_dolmen import model_problem_text


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--fresh", type=Path, required=True)
    parser.add_argument("--native-models", type=Path, required=True)
    parser.add_argument("--reference-models", type=Path, required=True)
    args = parser.parse_args()
    origins = json.loads((args.corpus / "metadata.json").read_text())
    by_hash = {row["sha256"]: row for row in origins["records"]}
    inputs = {
        Path(name).name: name
        for name in (args.corpus / "manifest.txt").read_text().splitlines()
    }
    fresh = json.loads(args.fresh.read_text())
    assert fresh["solver_sha256"] == origins["binary_sha256"]
    assert len(inputs) == len(by_hash) == len(fresh["records"])
    proofs = {"native": {}, "reference": {}}
    for label, directory in [
        ("native", args.native_models),
        ("reference", args.reference_models),
    ]:
        for path in sorted(directory.glob("shard-*/summary.csv")):
            metadata = json.loads(path.with_name("metadata.json").read_text())
            assert (
                metadata["status"] == "complete"
                and metadata["require_dolmen_pass"]
                and metadata["expect_sat"]
            )
            if label == "native":
                assert "--check-model" in metadata["solver_args"]
            for index, row in enumerate(csv.DictReader(path.open())):
                name = Path(row["file"]).name
                assert name in inputs and name not in proofs[label]
                assert (
                    row["answer"],
                    row["outcome"],
                    row["validator"],
                    row["solver_exit"],
                    row["dolmen_exit"],
                ) == ("sat", "pass", "dolmen", "0", "0")
                adapted = path.parent / f"{index:05d}-{name}.smt2"
                assert adapted.read_text() == model_problem_text(
                    (args.corpus / inputs[name]).read_text()
                )
                proofs[label][name] = {
                    "outcome": "pass",
                    "validator": "dolmen",
                    "adapted_sha256": digest(adapted),
                }
        expected = {
            Path(row["file"]).name
            for row in fresh["records"]
            if row[label]["status"] == "ok" and row[label]["answer"] == "sat"
        }
        assert set(proofs[label]) == expected
    records, truth = [], {}
    for row in fresh["records"]:
        name = Path(row["file"]).name
        source = args.corpus / inputs[name]
        sha = digest(source)
        assert sha == source.stem == row["input_sha256"]
        syntax = check_conjunction(source.read_text())
        assert syntax["expected"] == "unknown"
        origin = by_hash[sha]
        decisive = {
            run["answer"]
            for run in (row["native"], row["reference"])
            if run["status"] == "ok" and run["answer"] in ("sat", "unsat")
        }
        for occurrence in origin["occurrences"]:
            completion = occurrence["capture"]["completion"]
            if completion:
                decisive.add("unsat" if completion["conflict"] else "sat")
        assert len(decisive) <= 1, "capture/replay answer disagreement"
        expected = "unknown"
        if name in proofs["native"] or name in proofs["reference"]:
            expected = "sat"
        elif (
            row["reference"]["status"] == "ok" and row["reference"]["answer"] == "unsat"
        ):
            expected = "unsat"
        if expected != "unknown":
            assert decisive == {expected}
        truth[inputs[name]] = expected
        a = row["analysis"]
        records.append(
            {
                **origin,
                "expected": expected,
                "syntax": syntax,
                "native": row["native"],
                "reference": row["reference"],
                "observed_fresh_checks": a["calls"],
                "completed_check_pivots": a["total_pivots"] if a["calls"] else None,
                "completed_check_ns": a["total_check_ns"] if a["calls"] else None,
                "native_model": proofs["native"].get(name),
                "reference_model": proofs["reference"].get(name),
            }
        )
    result = {
        **origins,
        "status": "partially-classified-captured-call-corpus",
        "fresh_timeout_seconds": fresh["timeout_seconds"],
        "fresh_version": fresh["version"],
        "native_model_passes": len(proofs["native"]),
        "reference_model_passes": len(proofs["reference"]),
        "results": records,
    }
    (args.corpus / "classification.json").write_text(
        json.dumps(result, indent=2) + "\n"
    )
    (args.corpus / "expected-statuses.json").write_text(
        json.dumps(dict(sorted(truth.items())), indent=2) + "\n"
    )
    for group, selected in [
        ("known", [name for name, value in truth.items() if value != "unknown"]),
        ("unknown", [name for name, value in truth.items() if value == "unknown"]),
    ]:
        (args.corpus / ("manifest-" + group + ".txt")).write_text(
            "".join(name + "\n" for name in sorted(selected))
        )
    print(
        "classified",
        sum(value != "unknown" for value in truth.values()),
        "unknown",
        sum(value == "unknown" for value in truth.values()),
    )


if __name__ == "__main__":
    main()
