#!/usr/bin/env python3
"""Verify the selected publication shards and report counts without hiding timeouts.

Run in the experiment workspace: CSV paths still refer to retained inputs there.
Only metadata and hashes enter the public report; generated inputs stay in the
experiment artifacts. This command does not execute a solver.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from quantifier_preparation_gate import ORDERS
from summarize_quantifier_preparation_gate import verify_case

ROUTES = ("QF_UF", "QF_LRA", "QF_LIA", "QF_UFLRA", "QF_UFLIA",
          "UF", "incremental_QF_UF", "incremental_QF_LRA")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text())


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def models(path: Path, expected: set[Path]) -> int:
    if not expected:
        require(not path.exists() or not read_csv(path), "unexpected model rows")
        return 0
    rows = read_csv(path)
    files = [Path(row["file"]).resolve() for row in rows]
    require(len(files) == len(set(files)) and set(files) == expected,
            "missing, duplicate or unexpected model input")
    for row in rows:
        require(row["outcome"] == "pass" and row["validator"] == "dolmen"
                and row["solver_exit"] == "0" and row["dolmen_exit"] == "0",
                "model replay did not pass")
    return len(rows)


def summarize_shard(folder: Path, slot: int, candidate: str,
                    resource_triage: dict | None = None) -> dict:
    require((folder / "exit-code").read_text().strip() == "0", "failed shard")
    version = (folder / "version.txt").read_text()
    require(f"git: {candidate[:7]}" in version.splitlines(), "wrong revision")
    hashes = {line.split()[1]: line.split()[0]
              for line in (folder / "executables.sha256").read_text().splitlines()}
    require(len(hashes) == 3, "missing executable hashes")
    gate = folder / "gate"
    result = {"slot": slot, "route": ROUTES[slot // 4], "artifact": str(folder),
              "binaries": hashes, "cases": 250, "pass": 0, "timeout": 0,
              "sat_models": 0, "queries": 0, "reference_timeout": 0, "input_sha256": [],
              "resource_incomplete": 0, "resource_triage": []}
    if slot < 20:
        rows = read_csv(gate / "summary.csv")
        require(len(rows) == 250, "missing answer rows")
        inputs = [Path(row.get("case") or row["file"]).resolve() for row in rows]
        require(len(set(inputs)) == 250, "duplicated input path")
        sat = set()
        for row, path in zip(rows, inputs):
            answer = row.get("ours", row.get("answer"))
            outcome = row["outcome"]
            reference = row.get("reference", row.get("refs", row.get("references")))
            if outcome == "UNKNOWN_INVOLVED":
                input_hash = digest(path)
                triage = (resource_triage or {}).get(input_hash)
                require(triage is not None and triage["route"] == result["route"]
                        and answer == "unknown" and reference == triage["reference_answer"]
                        and row.get("ours_code") == "0", "untriaged unknown result")
                result["resource_incomplete"] += 1
                result["resource_triage"].append(input_hash)
            elif outcome == "PASS":
                require(answer in {"sat", "unsat"} and reference == answer,
                        "reference mismatch or nondefinite pass")
                require(row.get("ours_code", "0") == "0"
                        and row.get("reference_code", "0") == "0", "post-answer error")
                result["pass"] += 1
                if answer == "sat":
                    sat.add(path)
                    if 4 <= slot < 8:
                        require(row["dolmen"] == "PASS", "missing inline validation")
            else:
                require(slot // 4 == 2 and outcome == "TIMEOUT"
                        and answer in {"sat", "unsat", "TIMEOUT"}
                        and reference in {"sat", "unsat", "TIMEOUT"}
                        and "TIMEOUT" in {answer, reference}, "unexpected resource outcome")
                result["timeout"] += 1
                result["reference_timeout"] += int(reference == "TIMEOUT")
        result["input_sha256"] = [digest(path) for path in inputs]
        if 4 <= slot < 8:
            result["sat_models"] = len(sat)
        else:
            model_csv = (gate / "model-validation" if slot < 4 else folder / "models") / "summary.csv"
            result["sat_models"] = models(model_csv, sat)
    elif slot < 24:
        rows = [json.loads(line) for line in (gate / "rows.jsonl").read_text().splitlines()]
        cases = defaultdict(list)
        for row in rows:
            cases[row["index"]].append(row)
        require(set(cases) == set(range(250)), "missing quantified cases")
        profiles = {f"primo-{order}" for order in ORDERS} | {"ref0", "primo-preprocess-only"}
        counts = Counter()
        for index, group in cases.items():
            verify_case(group, profiles)
            actual_hash = digest(gate / f"case-{index:05d}/input.smt2")
            require(group[0]["input_sha256"] == actual_hash, "input changed after run")
            for row in group:
                allowed = {"unknown", "unsat"} if row["profile"] == "primo-preprocess-only" else {"sat", "unsat"}
                require(row["answer"] in allowed, "incomplete quantified result")
                counts[f'{row["profile"]}:{row["answer"]}'] += 1
            result["input_sha256"].append(actual_hash)
        result["pass"] = 250
        result["profile_answers"] = dict(counts)
    else:
        command = read_json(gate / "run.json")["command"]
        result["seed_begin"] = int(command[command.index("--seed") + 1])
        summaries = sorted(gate.glob("session-*/result.json"))
        require(len(summaries) == 250, "missing sessions")
        for path in summaries:
            row = read_json(path)
            invocation = read_json(path.with_name("invocation.json"))
            require(row["outcome"] == "pass", "failed session")
            require(len(row["queries"]) == (9 if slot < 28 else 11), "missing session queries")
            for query in row["queries"]:
                require(query["outcome"] == "pass" and query["answer"] in {"sat", "unsat"},
                        "failed session query")
                if query["answer"] == "sat":
                    require(bool(query.get("dolmen_command")), "unchecked session SAT model")
                    result["sat_models"] += 1
            result["queries"] += len(row["queries"])
            result["input_sha256"].append(invocation["input_sha256"])
        result["pass"] = 250
        summary = read_json(gate / "summary.json")
        for key in ("pass", "sat_models", "queries"):
            require(summary[key] == result[key], f"inconsistent session {key}")
    return result


def aggregate(shards: list[dict]) -> dict:
    require(sorted(s["slot"] for s in shards) == list(range(32)), "missing or duplicate shards")
    require(len({json.dumps(s["binaries"], sort_keys=True) for s in shards}) == 1,
            "mixed executable hashes")
    routes = {}
    for name in ROUTES:
        group = [s for s in shards if s["route"] == name]
        hashes = [h for s in group for h in s["input_sha256"]]
        routes[name] = {key: sum(s[key] for s in group)
                        for key in ("cases", "pass", "timeout", "sat_models", "queries")}
        routes[name]["resource_incomplete"] = sum(s.get("resource_incomplete", 0) for s in group)
        routes[name]["reference_timeout"] = sum(s.get("reference_timeout", 0) for s in group)
        routes[name]["distinct_inputs"] = len(set(hashes))
        # Session generators use seed + index: overlap means a broken sharding recipe.
        if name.startswith("incremental_"):
            seeds = [seed for shard in group for seed in
                     range(shard["seed_begin"], shard["seed_begin"] + shard["cases"])]
            require(len(set(seeds)) == len(seeds), "overlapping session seed ranges")
    return {"routes": routes, "shards": shards}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--initial", required=True)
    parser.add_argument("--integer", required=True)
    parser.add_argument("--sessions", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    shards = []
    for slot in range(32):
        run = args.sessions if slot >= 20 else args.integer if 8 <= slot < 12 else args.initial
        shards.append(summarize_shard(args.root / run / f"shard-{slot}", slot, args.candidate))
    report = aggregate(shards)
    for shard in report["shards"]:
        inputs = shard.pop("input_sha256")
        shard["distinct_inputs"] = len(set(inputs))
        shard["input_manifest_sha256"] = hashlib.sha256(
            ("\n".join(sorted(inputs)) + "\n").encode()).hexdigest()
    report["candidate"] = args.candidate
    report["scope"] = "Correctness sample; timeouts are incomplete, not passes. No speed claim."
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["routes"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
