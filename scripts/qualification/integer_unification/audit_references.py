#!/usr/bin/env python3
"""Join frozen integer timing answers to saved references without running solvers.

Completed per-row JSONL records from interrupted reference shards are usable for
answer comparison, but never treated as complete reference timing campaigns.
"""

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def solved(row):
    return (row["status"] == "ok" and str(row["exit_code"]) == "0" and
            str(row["timeout"]).lower() == "false" and row["answer"] in ("sat", "unsat"))


def audit(candidate, reference, output):
    records = {}
    artifacts = {}
    for logic in ("QF_LIA", "QF_UFLIA"):
        for meta_path in sorted((reference / logic).glob("*/shard-*/provenance.json")):
            meta = json.loads(meta_path.read_text())
            source = meta_path.parent / "answers.csv"
            if source.exists():
                with source.open(newline="") as stream:
                    rows = list(csv.DictReader(stream))
            else:
                source = meta_path.parent / "answers.csv.jsonl"
                if not source.exists():
                    continue
                rows = [json.loads(line) for line in source.read_text().splitlines()]
            names = [r["file"] for r in rows]
            if len(names) != len(set(names)) or set(names) - set(meta["inputs"]):
                raise ValueError(f"malformed reference rows: {source}")
            artifacts[str(meta_path)] = digest(meta_path)
            artifacts[str(source)] = digest(source)
            for row in rows:
                if solved(row):
                    key = (logic, row["file"], meta["inputs"][row["file"]])
                    records.setdefault(key, []).append({
                        "answer": row["answer"], "source": str(source),
                        "binary_sha256": meta["solver"]["sha256"],
                        "partial_campaign": meta["status"] != "complete"})
    reports = {}
    details = []
    for logic in ("QF_LIA", "QF_UFLIA"):
        for round_id in range(2):
            counts = Counter()
            arm = candidate / logic / f"round-{round_id}/candidate"
            for source in sorted(arm.glob("shard-*/answers.csv")):
                meta_path = source.parent / "provenance.json"
                meta = json.loads(meta_path.read_text())
                artifacts[str(source)] = digest(source)
                artifacts[str(meta_path)] = digest(meta_path)
                with source.open(newline="") as stream:
                    rows = list(csv.DictReader(stream))
                for row in rows:
                    key = (logic, row["file"], meta["inputs"][row["file"]])
                    refs = records.get(key, [])
                    answers = {r["answer"] for r in refs}
                    if len(answers) > 1:
                        outcome = "reference-disagreement"
                    elif not solved(row):
                        outcome = "candidate-unsolved"
                    elif not answers:
                        outcome = "no-decisive-reference"
                    elif row["answer"] not in answers:
                        outcome = "wrong-answer"
                    else:
                        outcome = "agree"
                    counts[outcome] += 1
                    details.append({"logic": logic, "round": round_id, "file": row["file"],
                                    "sha256": key[2], "candidate": row["answer"],
                                    "outcome": outcome, "references": refs})
            reports[f"{logic}/round-{round_id}"] = dict(counts)
    totals = sum((Counter(c) for c in reports.values()), Counter())
    result = {"status": "failed" if totals["wrong-answer"] or totals["reference-disagreement"]
              else "joined", "counts": reports, "totals": totals, "artifacts": artifacts,
              "note": "Hash-matched recorded answer comparison, including completed rows "
                      "from partial reference campaigns. Unavailable reference answers remain "
                      "explicit; no UNSAT proof certification or new timing claim.",
              "rows": details}
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"status": result["status"], "counts": reports, "totals": totals}))
    return int(result["status"] == "failed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    raise SystemExit(audit(args.candidate, args.reference, args.out))
