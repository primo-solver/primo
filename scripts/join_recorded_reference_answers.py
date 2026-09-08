#!/usr/bin/env python3
"""Join candidate answers to hash-matched recorded references; launch no solvers."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path

from summarize_component_pairs import load_arm, solved


def compare(candidate: Path, references: list[Path]) -> dict:
    rows, metadata = load_arm(candidate)
    if not rows:
        raise ValueError("no candidate rows")
    hashes = {name: digest for shard in metadata.values()
              for name, digest in shard["inputs"].items()}
    answers: dict[str, list[dict]] = {}
    skipped = []
    for root in references:
        for shard in sorted(root.glob("shard-*")):
            provenance = json.loads((shard / "provenance.json").read_text())
            if provenance.get("status") != "complete" or not provenance.get("inputs_unchanged"):
                skipped.append(str(shard))
                continue
            with (shard / "answers.csv").open(newline="") as stream:
                records = list(csv.DictReader(stream))
            if len(records) != len(provenance["inputs"]) or {
                row["file"] for row in records
            } != set(provenance["inputs"]):
                raise ValueError(f"reference rows do not cover provenance: {shard}")
            for row in records:
                name = row["file"]
                if name not in rows:
                    continue
                if hashes[name] != provenance["inputs"][name]:
                    raise ValueError(f"reference input hash mismatch: {shard}: {name}")
                if solved(row):
                    answers.setdefault(name, []).append({
                        "answer": row["answer"], "source": str(shard),
                        "binary": provenance["solver"], "seconds": row["seconds"],
                    })
    counts: Counter[str] = Counter()
    details = []
    for name, row in rows.items():
        recorded = answers.get(name, [])
        known = {item["answer"] for item in recorded}
        if len(known) > 1:
            outcome = "reference-disagreement"
        elif not solved(row):
            outcome = "candidate-unsolved"
        elif not known:
            outcome = "no-decisive-reference"
        elif row["answer"] in known:
            outcome = "agree"
        else:
            outcome = "wrong-answer"
        counts[outcome] += 1
        details.append({"file": name, "sha256": hashes[name], "outcome": outcome,
                        "candidate": row["answer"], "references": recorded})
    status = "failed" if counts["wrong-answer"] or counts["reference-disagreement"] else (
        "incomplete" if counts["no-decisive-reference"] else "compared")
    return {"status": status, "counts": dict(counts), "candidate": str(candidate),
            "reference_roots": [str(root) for root in references],
            "skipped_incomplete_reference_shards": skipped, "rows": details,
            "note": "No solver executed. Unsolved candidate rows are not verified answers; "
                    "model validation and timing acceptance are separate gates."}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--reference", type=Path, action="append", required=True)
    args = parser.parse_args()
    try:
        report = compare(args.candidate, args.reference)
    except (OSError, ValueError, KeyError) as error:
        report = {"status": "incomplete", "diagnostic": str(error)}
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "compared" else 1


if __name__ == "__main__":
    raise SystemExit(main())
