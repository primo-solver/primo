#!/usr/bin/env python3
"""Verify complete gate artifacts before summarizing answer coverage."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path

from quantifier_preparation_gate import DEFINITE, DOMAINS, ORDERS


def verify_case(rows: list[dict], profiles: set[str]) -> None:
    labels = [row["profile"] for row in rows]
    if len(labels) != len(profiles) or set(labels) != profiles:
        raise ValueError("missing or duplicated case profile")
    if len({row["input_sha256"] for row in rows}) != 1:
        raise ValueError("profiles used different inputs")
    for row in rows:
        answer = row["answer"]
        if answer not in DEFINITE | {"unknown", "timeout"}:
            raise ValueError(f"blocking solver outcome: {answer}")
        if answer != "timeout" and row["exit"] != 0:
            raise ValueError("answer followed by unsuccessful exit")
    if len({row["answer"] for row in rows if row["answer"] in DEFINITE}) > 1:
        raise ValueError("definite answer disagreement")


def summarize(root: Path) -> dict:
    modes = {"generated": (3000, 30), "mutations": (1000, 20), "arithmetic": (1000, 10)}
    result: dict = {"modes": {}, "binaries": {}, "commit": None}
    for mode, (count, shards) in modes.items():
        profiles = {
            f"primo-{domain}-{order}"
            for domain in DOMAINS
            for order in (ORDERS if mode == "generated" else ("sum",))
        } | {"ref0", "ref1"}
        totals: Counter[str] = Counter()
        unconfirmed = []
        seen = set()
        input_hashes = set()
        for shard in range(shards):
            folder = root / f"{mode}-{shard}"
            run = json.loads((folder / "run.json").read_text())
            if run["args"]["count"] != count or run["args"]["shards"] != shards:
                raise ValueError("wrong gate coverage configuration")
            if run["args"]["shard_index"] != shard or run["args"]["mode"] != mode:
                raise ValueError("wrong shard identity")
            summary = json.loads((folder / "summary.json").read_text())
            if summary["failures"] or summary["counts"].get("failed", 0):
                raise ValueError("gate reported an oracle mismatch or solver failure")
            if result["commit"] not in {None, run["commit"]}:
                raise ValueError("mixed candidate revisions")
            result["commit"] = run["commit"]
            for label, binary in run["binaries"].items():
                if label in result["binaries"] and result["binaries"][label] != binary:
                    raise ValueError("mixed executable identities")
                result["binaries"][label] = binary
            cases: dict[int, list[dict]] = {}
            for line in (folder / "rows.jsonl").read_text().splitlines():
                row = json.loads(line)
                cases.setdefault(row["index"], []).append(row)
            if set(cases) != set(range(shard, count, shards)):
                raise ValueError("incomplete or mis-sharded cases")
            if summary["counts"]["cases"] != len(cases):
                raise ValueError("summary and retained rows disagree on coverage")
            for index, rows in cases.items():
                if index in seen:
                    raise ValueError("duplicate case index")
                seen.add(index)
                verify_case(rows, profiles)
                source = folder / f"case-{index:05d}" / "input.smt2"
                digest = hashlib.sha256(source.read_bytes()).hexdigest()
                if digest != rows[0]["input_sha256"]:
                    raise ValueError("retained input hash mismatch")
                input_hashes.add(digest)
                refs = {
                    row["answer"] for row in rows if row["profile"].startswith("ref")
                }
                for row in rows:
                    totals[f"{row['profile']}:{row['answer']}"] += 1
                    if row["profile"].startswith("primo") and row["answer"] in DEFINITE:
                        if refs & DEFINITE:
                            totals["reference-confirmed-candidate-answers"] += 1
                        else:
                            unconfirmed.append(
                                {
                                    "index": index,
                                    "profile": row["profile"],
                                    "answer": row["answer"],
                                }
                            )
                totals[
                    "reference-definite-inputs"
                    if refs & DEFINITE
                    else "reference-inconclusive-inputs"
                ] += 1
        result["modes"][mode] = {
            "cases": len(seen),
            "distinct_input_hashes": len(input_hashes),
            "counts": dict(totals),
            "unconfirmed": unconfirmed,
        }
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.root)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {
                mode: {"cases": data["cases"], "unconfirmed": len(data["unconfirmed"])}
                for mode, data in result["modes"].items()
            }
        )
    )


if __name__ == "__main__":
    main()
