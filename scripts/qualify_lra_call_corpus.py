#!/usr/bin/env python3
"""Join the single-check conjunction pilot to exact inputs and direct model checks."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from collections import Counter
from pathlib import Path

from prepare_lra_call_replay import check_conjunction
from validate_qfuf_models_with_dolmen import model_problem_text


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--fresh", type=Path, required=True)
    parser.add_argument("--models", type=Path, required=True)
    parser.add_argument("--native-source", required=True)
    parser.add_argument("--model-job", required=True)
    parser.add_argument("--dolmen-sha256", required=True)
    parser.add_argument("--cutoff", type=int, default=128)
    args = parser.parse_args()
    fresh = json.loads(args.fresh.read_text())
    occurrences = {
        row["sha256"]: row
        for row in json.loads((args.corpus / "occurrences.json").read_text())
    }
    files = (args.corpus / "manifest.txt").read_text().splitlines()
    inputs = {Path(name).name: name for name in files}
    assert len(inputs) == len(files) == len(occurrences) == len(fresh["records"])
    assert "git: " + args.native_source[:7] in fresh["version"]
    models = {}
    for path in sorted(args.models.glob("shard-*/summary.csv")):
        metadata = json.loads(path.with_name("metadata.json").read_text())
        assert metadata["status"] == "complete" and metadata["require_dolmen_pass"]
        assert metadata["expect_sat"] and "--check-model" in metadata["solver_args"]
        for index, row in enumerate(csv.DictReader(path.open())):
            name = Path(row["file"]).name
            assert name in inputs and name not in models
            assert (
                row["answer"],
                row["outcome"],
                row["validator"],
                row["solver_exit"],
                row["dolmen_exit"],
            ) == ("sat", "pass", "dolmen", "0", "0")
            source = args.corpus / inputs[name]
            adapted = path.parent / f"{index:05d}-{name}.smt2"
            assert adapted.read_text() == model_problem_text(source.read_text())
            models[name] = {
                "validator": "dolmen",
                "outcome": "pass",
                "adapted_sha256": sha256(adapted),
            }
    records, expected_sat = [], set()
    for row in fresh["records"]:
        name = Path(row["file"]).name
        source = args.corpus / inputs[name]
        digest = sha256(source)
        assert digest == source.stem == row["input_sha256"]
        origin = occurrences[digest]
        assert check_conjunction(source.read_text()) == origin["syntax"]
        assert (
            row["native"]["status"] == "ok"
            and row["native"]["answer"] == origin["syntax"]["expected"]
        )
        analysis = row["analysis"]
        assert analysis["coverage"] == "complete" and analysis["calls"] == 1
        reference = row["reference"]
        if row["native"]["answer"] == "sat":
            expected_sat.add(name)
            assert name in models
            assert reference["status"] == "timeout" or (
                reference["status"] == "ok" and reference["answer"] == "sat"
            )
        else:
            assert reference["status"] == "ok" and reference["answer"] == "unsat"
        records.append(
            {
                "file": inputs[name],
                "sha256": digest,
                "syntax": origin["syntax"],
                "source_families": sorted(
                    {x["parent"].split("/")[0] for x in origin["occurrences"]}
                ),
                "warm_occurrences": origin["occurrences"],
                "fresh_pivots": analysis["max_pivots"],
                "bland_fallbacks": int(row["native"]["stats"][":lra.bland-fallbacks"]),
                "fresh_check_ns": analysis["total_check_ns"],
                "native_wall_seconds": row["native"]["seconds"],
                "reference": reference,
                "model_check": models.get(name),
            }
        )
    assert set(models) == expected_sat
    assert {row["file"] for row in records} == set(files)
    records.sort(key=lambda row: row["file"])
    hard = [row["file"] for row in records if row["fresh_pivots"] >= args.cutoff]
    controls = [row["file"] for row in records if row["fresh_pivots"] < args.cutoff]
    (args.corpus / f"fresh-ge{args.cutoff}.txt").write_text(
        "".join(name + "\n" for name in hard)
    )
    (args.corpus / f"fresh-lt{args.cutoff}-controls.txt").write_text(
        "".join(name + "\n" for name in controls)
    )
    timings = sorted(row["fresh_check_ns"] for row in records)
    result = {
        "schema": 1,
        "status": "qualified-pilot-not-final-ablation-selection",
        "native_source": args.native_source,
        "binary_sha256": fresh["solver_sha256"],
        "native_version": fresh["version"],
        "native_solver_args": [
            "--quiet",
            "--stats",
            *fresh.get("solver_args", []),
            "--lra-call-profile",
            "PER_INPUT_TRACE",
        ],
        "reference_sha256": fresh["reference_sha256"],
        "timeout_seconds": fresh["timeout_seconds"],
        "timing_job_ids": fresh["job_ids"],
        "model_job": args.model_job,
        "dolmen_sha256": args.dolmen_sha256,
        "model_options": [
            "--check-model",
            "--require-dolmen-pass",
            "--expect-sat",
            "--dolmen-arg=--strict=false",
            "--dolmen-size=2G",
            "--timeout=60",
            "--dolmen-timeout=30",
        ],
        "inputs": len(records),
        "expected_status": dict(Counter(row["syntax"]["expected"] for row in records)),
        "direct_dolmen_model_passes": len(models),
        "native_outcomes": fresh["all_observed"]["native_outcomes"],
        "reference_outcomes": fresh["all_observed"]["reference_outcomes"],
        "cold_pivot_cutoff": args.cutoff,
        "fresh_ge_cutoff": len(hard),
        "fresh_below_cutoff_controls": len(controls),
        "fresh_pivot_quantiles": fresh["all_observed"]["pivot_quantiles"],
        "fresh_bland_fallback_calls": sum(row["bland_fallbacks"] for row in records),
        "fresh_check_ns": {
            "min": timings[0],
            "median": timings[math.ceil(len(timings) * 0.5) - 1],
            "p90": timings[math.ceil(len(timings) * 0.9) - 1],
            "max": timings[-1],
            "sum": sum(timings),
        },
        "limitations": [
            "Warm basis, assignment, bound-reason graph and original Boolean context are omitted.",
            "Input order and pivot counts belong to this baseline; freeze manifests before comparing variants.",
            "Selection is biased toward first qualifying calls of high observed-max-pivot source parents.",
            f"{sum(row['reference']['status'] == 'timeout' for row in records)} SAT reference timeouts are covered by direct independent model validation, not counted as Z3 agreements.",
        ],
        "records": records,
    }
    (args.corpus / "metadata.json").write_text(json.dumps(result, indent=2) + "\n")
    print(
        f"{len(records)} qualified conjunctions; {len(hard)} baseline checks >= {args.cutoff} pivots; {len(models)} direct model passes"
    )


if __name__ == "__main__":
    main()
