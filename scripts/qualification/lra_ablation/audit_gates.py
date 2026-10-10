#!/usr/bin/env python3
"""Require complete generated and independently replayed mutation coverage."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from run import PROFILES, digest, rows, write


def audit(root: Path, binary_hash: str, generated_only: bool = False) -> dict:
    reports = {}
    mutation_count = None
    for name, arguments in PROFILES:
        counts = {"generated": 0, "generated_timeouts": 0, "generated_sat_models": 0,
                  "boolean_arithmetic": 0, "boolean_sat_models": 0, "mutant_replays": 0}
        for shard in range(8):
            out = root / "generated-gates" / name / f"shard-{shard}"
            identity = json.loads((out / "execution-gate.json").read_text())
            if identity["binary_sha256"] != binary_hash or identity["solver_args"] != arguments:
                raise ValueError("gate binary or options mismatch")
            if shard < 6:
                record = json.loads((out / "gate-complete.json").read_text())
                for path, key in (("answers/summary.csv", "answer_summary_sha256"),
                                  ("status-aware-answers.csv", "status_aware_summary_sha256")):
                    if digest(out / path) != record[key]:
                        raise ValueError("generated answer parent changed")
                answers = rows(out / "status-aware-answers.csv")
                selected = {r["file"] for r in answers if r["outcome"] == "PASS" and r["ours"] == "sat"}
                model_rows = rows(out / "models/summary.csv") if selected else []
                if (record["rows"] != 500 or len(answers) != 500
                        or len(model_rows) != len(selected) or {r["file"] for r in model_rows} != selected
                        or any(r["outcome"] != "pass" or r["validator"] != "dolmen"
                               or r["answer"] != "sat" or r["solver_exit"] != "0"
                               or r["dolmen_exit"] != "0" for r in model_rows)):
                    raise ValueError("generated model coverage is not complete")
                for row in answers:
                    timeout_only = (row["outcome"] == "INCOMPLETE" and
                                    "timeout" in (row["ours"], row["reference"]) and
                                    row["ours"] in {"sat", "unsat", "timeout"} and
                                    row["reference"] in {"sat", "unsat", "timeout"} and
                                    row["ours_exit"] in {"0", "124"} and
                                    row["reference_exit"] in {"0", "124"})
                    if row["outcome"] != "PASS" and not timeout_only:
                        raise ValueError("generated answers require triage")
                counts["generated"] += 500
                counts["generated_timeouts"] += record["timeouts"]
                counts["generated_sat_models"] += len(model_rows)
            else:
                observed = rows(out / "boolean-arithmetic/summary.csv")
                if len(observed) != 500 or any(r["outcome"] != "PASS" for r in observed):
                    raise ValueError("Boolean/arithmetic gate is incomplete or failed")
                if any(r["answer"] == "sat" and r["dolmen"] != "PASS" for r in observed):
                    raise ValueError("Boolean/arithmetic SAT model lacks Dolmen acceptance")
                counts["boolean_arithmetic"] += 500
                counts["boolean_sat_models"] += sum(r["answer"] == "sat" for r in observed)
        if generated_only:
            reports[name] = counts
            continue
        seen = {}
        for shard in range(8):
            out = root / "retained-mutant-replay" / name / f"shard-{shard}"
            identity = json.loads((out / "execution-mutants.json").read_text())
            if identity["binary_sha256"] != binary_hash or identity["solver_args"] != arguments:
                raise ValueError("mutant replay binary or options mismatch")
            inputs = json.loads((out / "input-identities.json").read_text())
            observed = rows(out / "answers.csv")
            if len(observed) != len(inputs) or any(r["outcome"] != "PASS" or r["ours_exit"] != "0"
                                                   or r["reference_exit"] != "0" for r in observed):
                raise ValueError("retained mutant replay failed or has missing rows")
            if set(seen) & set(inputs):
                raise ValueError("duplicate mutant across shards")
            seen.update(inputs)
            counts["mutant_replays"] += len(observed)
        if mutation_count is None:
            mutation_count = seen
        elif seen != mutation_count:
            raise ValueError("profiles replayed different mutation inputs")
        reports[name] = counts
    return {"status": "generated-qualified-mutation-pending" if generated_only else "qualified",
            "binary_sha256": binary_hash, "profiles": reports,
            "wrong_answers": 0, "crashes": 0, "parser_type_errors": 0,
            "invalid_models": 0, "untriaged_reference_disagreements": 0,
            "mutation_inputs": len(mutation_count or {}),
            "distinct_mutation_hashes": len(set((mutation_count or {}).values()))}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--binary-sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--generated-only", action="store_true")
    args = parser.parse_args()
    report = audit(args.root, args.binary_sha256, args.generated_only)
    write(args.out, report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
