#!/usr/bin/env python3
"""Validate collected diagnostic profiles and extract instruction counts."""

import argparse
import hashlib
import json
import re
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def annotations(path):
    text = path.read_text()
    total = re.search(r"^\s*([\d,]+).*PROGRAM TOTALS$", text, re.MULTILINE)
    if total is None:
        raise ValueError("missing instruction total: " + str(path))
    functions = {}
    for name in (
        "check",
        "soi_select",
        "pivot",
        "update",
        "set_soi_direction",
        "quickxplain",
    ):
        values = re.findall(
            r"^\s*([\d,]+).*?\?\?\?:primo::theory::arithmetic::LraTableau::"
            + name
            + r"\(",
            text,
            re.MULTILINE,
        )
        # Missing symbols can be below the annotation threshold or inlined.
        functions[name] = (
            sum(int(v.replace(",", "")) for v in values) if values else None
        )
    return int(total[1].replace(",", "")), functions


def stats(path):
    return {
        match[1]: int(match[2])
        for match in re.finditer(
            r"^(:lra\.[^\s]+) (\d+)$", path.read_text(), re.MULTILINE
        )
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profiles", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    plan_path = Path(__file__).with_name("plan.json")
    plan = json.loads(plan_path.read_text())
    rows = []
    drivers = set()
    hashes = {}
    configurations = ["sparse-g1-f0", "soi-g1-f0", "soi-g1-f1"]
    for index, case in enumerate(plan["cases"]):
        directory = args.profiles / ("case-" + str(index))
        metadata = json.loads((directory / "metadata.json").read_text())
        assert metadata["accepted"] and metadata["binary_unchanged"]
        assert metadata["case"] == case
        assert metadata["binary_sha256"] == plan["binary_sha256"]
        assert metadata["plan_sha256"] == digest(plan_path)
        assert [r["configuration"] for r in metadata["runs"]] == configurations
        drivers.add(metadata["driver_sha256"])
        for run in metadata["runs"]:
            name = run["configuration"]
            native, instrumented = run["native"], run["instrumented"]
            assert native["complete"] and native["returncode"] == 0
            assert native["answers"] == [case["expected"]]
            assert instrumented["returncode"] in (0, 124, 137)
            if instrumented["returncode"] == 0:
                assert instrumented["complete"]
                assert instrumented["answers"] == [case["expected"]]
            native_stats = stats(directory / (name + "-native.stderr"))
            instrumented_stats = stats(directory / (name + "-instrumented.stderr"))
            if instrumented["complete"]:
                for counter in (
                    ":lra.pivots",
                    ":lra.bland-fallbacks",
                    ":lra.soi-minimize-calls",
                ):
                    assert native_stats[counter] == instrumented_stats[counter]
            total, inclusive = annotations(directory / (name + "-inclusive-yes.txt"))
            _, exclusive = annotations(directory / (name + "-inclusive-no.txt"))
            check, select = inclusive["check"], inclusive["soi_select"]
            rows.append(
                {
                    "case": index,
                    "file": case["file"],
                    "parent": case["parent"],
                    "configuration": name,
                    "host": metadata["host"],
                    "job": metadata["job"],
                    "expected": case["expected"],
                    "native_seconds": native["seconds"],
                    "native_stats": native_stats,
                    "profile_complete": instrumented["complete"],
                    "profile_returncode": instrumented["returncode"],
                    "profile_program_instructions": total,
                    "inclusive_instructions": inclusive,
                    "exclusive_instructions": exclusive,
                    "selector_fraction_of_check": select / check
                    if check and select
                    else None,
                }
            )
        for path in sorted(directory.iterdir()):
            if path.is_file():
                hashes[path.relative_to(args.profiles).as_posix()] = digest(path)
    result = {
        "native_commit": plan["native_commit"],
        "binary_sha256": plan["binary_sha256"],
        "plan_sha256": digest(plan_path),
        "executed_driver_sha256": sorted(drivers),
        "native_successful": len(rows),
        "native_wrong_answers": 0,
        "complete_profiles": sum(row["profile_complete"] for row in rows),
        "partial_profiles": sum(not row["profile_complete"] for row in rows),
        "method": "Callgrind 3.22.0 Ir inclusive/exclusive instruction counts, whole process; missing function counts are unavailable, not zero. Inclusive parents and children overlap. Only complete profiles support selector/check ratios. Native single replays are diagnostics, not a new benchmark comparison.",
        "rows": rows,
        "artifact_sha256": hashes,
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(
        result["native_successful"],
        "native;",
        result["complete_profiles"],
        "complete profiles;",
        result["partial_profiles"],
        "partial",
    )


if __name__ == "__main__":
    main()
