#!/usr/bin/env python3
"""Collect published, announced call captures without inventing expected verdicts."""

import argparse
import hashlib
import json
import shutil
from collections import Counter
from pathlib import Path

from prepare_lra_call_replay import check_conjunction


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    shards = [
        json.loads(p.read_text())
        for p in sorted(args.source.glob("shard-*/summary.json"))
    ]
    assert shards and len(shards) == shards[0]["shards"]
    assert all(
        row["complete"] and row["solver_sha256"] == shards[0]["solver_sha256"]
        for row in shards
    )
    cubes = args.out / "cubes"
    cubes.mkdir(parents=True, exist_ok=False)
    unique, coverage = {}, Counter()
    for shard in shards:
        for row in shard["records"]:
            capture = row["capture"]
            kind = capture.get("classification", "not-saved")
            coverage[kind] += 1
            assert not row["wrong"]
            if kind not in ("completed", "no-durable-completion"):
                continue
            event = capture["event"]
            assert event and event["file"] == "capture.smt2"
            source = (
                args.source
                / f"shard-{shard['index']}"
                / f"case-{row['index']:04d}"
                / "capture/capture.smt2"
            )
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            assert digest == capture["sha256"]
            assert check_conjunction(source.read_text())["expected"] == "unknown"
            if digest not in unique:
                shutil.copyfile(source, cubes / (digest + ".smt2"))
                unique[digest] = {
                    "sha256": digest,
                    "expected": "unknown",
                    "occurrences": [],
                }
            unique[digest]["occurrences"].append(
                {
                    "parent": row["file"],
                    "parent_sha256": row["input_sha256"],
                    "parent_status": row["native"]["status"],
                    "classification": kind,
                    "capture": event,
                }
            )
    (args.out / "manifest.txt").write_text(
        "".join("cubes/" + digest + ".smt2\n" for digest in sorted(unique))
    )
    (args.out / "metadata.json").write_text(
        json.dumps(
            {
                "status": "provisional-unclassified-captures",
                "source_version": shards[0]["version"],
                "binary_sha256": shards[0]["solver_sha256"],
                "source_timeout_seconds": shards[0]["timeout_seconds"],
                "source_manifest_sha256": shards[0]["manifest_sha256"],
                "coverage": dict(coverage),
                "unique_inputs": len(unique),
                "records": list(unique.values()),
            },
            indent=2,
        )
        + "\n"
    )
    print(dict(coverage), "unique inputs", len(unique))


if __name__ == "__main__":
    main()
