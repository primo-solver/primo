#!/usr/bin/env python3
"""Require complete generated and independently replayed mutation coverage."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from run import PROFILES, digest, rows, write


def invalid_mutations(root: Path) -> tuple[dict, dict]:
    """Accept only input-bound independent grammar errors, never trailing answers."""
    invalid = {}
    artifacts = {}
    for shard in range(4):
        directory = root / "mutant-triage" / f"shard-{shard}"
        selection = json.loads((directory / "selection.json").read_text())
        seen = {}
        for path in directory.glob("*.json"):
            if path.name == "selection.json":
                continue
            record = json.loads(path.read_text())
            source, checksum = record["file"], record["sha256"]
            dolmen, z3 = record["commands"]["dolmen"], record["commands"]["z3"]
            errors = re.findall(r"^Error (.*)$", dolmen["stderr"], re.MULTILINE)
            if (dolmen.get("exit") != 4 or z3.get("exit") != 1
                    or dolmen["command"] != ["contrib/local/bin/dolmen", "--strict=false"]
                    or z3["command"] != ["contrib/local/bin/z3"] or len(errors) != 1):
                raise ValueError("mutation exclusion lacks independent original-logic diagnostics")
            if errors[0] == "Forbidden expression in linear arithmetic":
                reason = "outside_linear_logic"
                if "logic does not support nonlinear arithmetic" not in z3["stdout"]:
                    raise ValueError("nonlinear mutation lacks independent reference diagnosis")
            elif errors[0].startswith("Unbound identifier:"):
                reason = "unbound_identifier"
                if "unknown constant" not in z3["stdout"]:
                    raise ValueError("unbound mutation lacks independent reference diagnosis")
            else:
                raise ValueError("unclassified grammar error")
            if source in invalid or source in seen:
                raise ValueError("duplicate mutation triage")
            seen[source] = checksum
            invalid[source] = {"sha256": checksum, "reason": reason}
            artifacts[str(path.relative_to(root))] = digest(path)
        if seen != selection["inputs"]:
            raise ValueError("mutation triage coverage mismatch")
        artifacts[str((directory / "selection.json").relative_to(root))] = digest(directory / "selection.json")
    return invalid, artifacts


def timeout_only(row: dict) -> bool:
    return (row["outcome"] == "INCOMPLETE"
            and "timeout" in (row["ours"], row["reference"])
            and all(row[answer] in {"sat", "unsat", "timeout"}
                    and row[code] == ("124" if row[answer] == "timeout" else "0")
                    for answer, code in (("ours", "ours_exit"), ("reference", "reference_exit"))))


def audit(root: Path, binary_hash: str, generated_only: bool = False) -> dict:
    reports = {}
    mutation_count = None
    invalid, triage_artifacts = ({}, {}) if generated_only else invalid_mutations(root)
    for name, arguments in PROFILES:
        counts = {"generated": 0, "generated_timeouts": 0, "generated_sat_models": 0,
                  "boolean_arithmetic": 0, "boolean_sat_models": 0, "mutant_replays": 0,
                  "mutant_pass": 0, "mutant_timeouts": 0, "invalid_mutations_excluded": 0}
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
                    generated_timeout_only = (row["outcome"] == "INCOMPLETE" and
                                    "timeout" in (row["ours"], row["reference"]) and
                                    row["ours"] in {"sat", "unsat", "timeout"} and
                                    row["reference"] in {"sat", "unsat", "timeout"} and
                                    row["ours_exit"] in {"0", "124"} and
                                    row["reference_exit"] in {"0", "124"})
                    if row["outcome"] != "PASS" and not generated_timeout_only:
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
            aliases = {f"{index}.smt2": source for index, source in enumerate(sorted(inputs))}
            if (len(observed) != len(inputs)
                    or {Path(r["file"]).name for r in observed} != set(aliases)):
                raise ValueError("retained mutant replay has missing or duplicated rows")
            excluded = set()
            for row in observed:
                source = aliases[Path(row["file"]).name]
                if source in invalid:
                    if (invalid[source]["sha256"] != inputs[source]
                            or row["reference"] != "crash" or row["reference_exit"] != "1"
                            or (row["ours"], row["ours_exit"]) not in {("crash", "2"), ("unsat", "0")}):
                        raise ValueError("mutation exclusion does not match the original error row")
                    excluded.add(source)
                    counts["invalid_mutations_excluded"] += 1
                elif timeout_only(row):
                    counts["mutant_timeouts"] += 1
                elif (row["outcome"] == "PASS" and row["ours_exit"] == row["reference_exit"] == "0"
                      and row["ours"] == row["reference"] and row["ours"] in {"sat", "unsat"}):
                    counts["mutant_pass"] += 1
                else:
                    raise ValueError(f"retained mutant needs triage: {row}")
            if excluded != set(inputs) & set(invalid):
                raise ValueError("triaged mutation exclusions have missing rows")
            if set(seen) & set(inputs):
                raise ValueError("duplicate mutant across shards")
            seen.update(inputs)
            counts["mutant_replays"] += len(observed)
        if mutation_count is None:
            mutation_count = seen
        elif seen != mutation_count:
            raise ValueError("profiles replayed different mutation inputs")
        reports[name] = counts
    if not generated_only and not set(invalid) <= set(mutation_count or {}):
        raise ValueError("triage contains inputs outside the replay corpus")
    return {"status": "generated-qualified-mutation-pending" if generated_only else "qualified",
            "binary_sha256": binary_hash, "profiles": reports,
            "wrong_answers": 0, "crashes": 0, "parser_type_errors": 0,
            "invalid_models": 0, "untriaged_reference_disagreements": 0,
            "mutation_inputs": len(mutation_count or {}),
            "invalid_mutation_inputs": invalid, "triage_artifact_sha256": triage_artifacts,
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
