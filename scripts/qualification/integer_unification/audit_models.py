#!/usr/bin/env python3
"""Audit frozen integer-migration replay joins; execute no solver or validator.

Run in the observation workspace after replay. Read the saved timing, replay,
and wider-logic triage records, checking identities and hashing saved artifacts.
A wider-logic pass is reported separately and never becomes a direct pass.
"""

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path

CANDIDATE = "f3a3904e9cf7628eece6fb4c30f80de9f29eafa4ab89eca5a48afbf9643901a8"
DOLMEN = "08b5fc3b17fb7eb54cb82141e9cabaf01039c686b2ac2512363344fbf23c052e"
LOGICS = ("QF_UF", "QF_LIA", "QF_LRA", "QF_UFLRA", "QF_UFLIA")


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def read(path):
    return json.loads(path.read_text())


def rows(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def parents(root):
    specs = []
    for logic, shards in (("QF_LIA", 256), ("QF_UFLIA", 16)):
        specs.append((f"integer-paired/{logic}", 2, shards))
    for logic in LOGICS:
        specs.extend([(f"integer-preservation-paired/{logic}", 2, 1),
                      (f"integer-preservation-reruns/{logic}", 3, 1)])
    for profile in ("default", "online", "lazy-uf", "offline-uflia"):
        specs.append((f"integer-shared-paired/{profile}", 2, 4))
    specs.extend([("observation-probe", 3, 3),
                  ("integer-shared-lazy-reruns", 3, 1),
                  ("integer-shared-online-loss", 3, 1)])
    for group, rounds, shards in specs:
        expected = {root / group / f"round-{r}/candidate/shard-{s}"
                    for r in range(rounds) for s in range(shards)}
        actual = {p.parent for p in (root / group).glob(
            "round-*/candidate/shard-*/provenance.json")}
        require(actual == expected, f"missing or extra shards: {group}")
        for parent in sorted(expected):
            yield group, parent


def audit(root, output, part, parts):
    triage_path = root / "integer-shared-paired/lra-model-triage/rows.json"
    triage = read(triage_path)
    exceptions = {(r["parent"], r["file"]): r for r in triage}
    require(len(exceptions) == len(triage) == 20, "triage coverage changed")
    used_exceptions = set()
    groups = {}
    evidence = []
    checked = {}

    def checked_hash(path):
        path = path.resolve()
        if path not in checked:
            checked[path] = digest(path)
        return checked[path]

    all_parents = list(parents(root))
    for group, parent in all_parents[part::parts]:
        counts = groups.setdefault(group, Counter())
        meta = read(parent / "provenance.json")
        require(meta["status"] == "complete" and meta["inputs_unchanged"],
                f"incomplete parent: {parent}")
        require(meta["solver"]["sha256"] == CANDIDATE, "wrong candidate")
        require(checked_hash(Path(meta["solver"]["path"])) == CANDIDATE,
                "candidate binary changed")
        timing = rows(parent / "answers.csv")
        require(len(timing) == len(meta["inputs"]) and
                {r["file"] for r in timing} == set(meta["inputs"]),
                f"timing manifest mismatch: {parent}")
        selected = [r for r in timing if r["status"] == "ok" and
                    r["answer"] == "sat" and r["exit_code"] == "0" and
                    r["timeout"] == "false"]
        expected = [str(Path(meta["root"]) / r["file"]) for r in selected]
        attempts = [parent / name for name in
                    ("model-validation-audit", "full-coverage", "model-validation")
                    if (parent / name / "coverage.json").exists()]
        require(bool(attempts), f"no replay: {parent}")
        dest = attempts[0]
        report = read(dest / "coverage.json")
        identity = report["provenance"]
        require(identity["parent_provenance_sha256"] == digest(parent / "provenance.json")
                and identity["parent_answers_sha256"] == digest(parent / "answers.csv"),
                f"parent identity mismatch: {parent}")
        require(identity["dolmen"]["sha256"] == DOLMEN and
                checked_hash(Path(identity["dolmen"]["path"])) == DOLMEN,
                "validator identity mismatch")
        require(identity["dolmen_args"] == ["--strict=false"], "validator flags changed")
        require(report["inputs_and_binary_unchanged"], "unstable replay")
        require(report["expected_sat"] == len(expected), "wrong expected count")
        require(not report["missing"] and not report["malformed_rows"], "coverage gap")
        require((dest / "sat-manifest.txt").read_text().splitlines() == expected,
                "SAT manifest mismatch")
        for name, checksum in report["artifacts"].items():
            artifact = dest / name
            require((digest(artifact) if artifact.exists() else None) == checksum,
                    f"changed artifact: {artifact}")
        require([arg for arg in report["command"] if arg.startswith("--solver-arg=")] ==
                [f"--solver-arg={arg}" for arg in meta["solver_args"] + ["--check-model"]],
                "solver profile mismatch")
        require(report["command"][report["command"].index("--solver") + 1] ==
                meta["solver"]["path"], "replay solver mismatch")
        summary = dest / "models/summary.csv"
        validated = rows(summary) if summary.exists() else []
        require([r["file"] for r in validated] == expected, "validation row mismatch")
        require(dict(Counter(r["outcome"] for r in validated)) == report["outcomes"],
                "outcome count mismatch")
        require(report["dolmen_pass"] == sum(r["outcome"] == "pass" for r in validated),
                "pass count mismatch")
        require(report["validator_exit"] in (0, 1), "validation process failed")
        if validated:
            vm = read(dest / "models/metadata.json")
            require(vm["status"] == "complete" and vm["expect_sat"] and
                    vm["require_dolmen_pass"] and vm["solver_args"] ==
                    meta["solver_args"] + ["--check-model"] and
                    vm["solver"] == meta["solver"]["path"] and
                    Path(vm["dolmen"]).resolve() == Path(identity["dolmen"]["path"]).resolve()
                    and vm["dolmen_args"] == identity["dolmen_args"] and
                    vm["count"] == vm["completed"] == len(expected) and
                    vm["outcomes"] == report["outcomes"], "validator metadata mismatch")
        counts["shards"] += 1
        counts["timing_rows"] += len(timing)
        counts["expected_sat"] += len(expected)
        for i, row in enumerate(validated):
            original = Path(row["file"])
            require(checked_hash(original) == meta["inputs"][selected[i]["file"]],
                    f"input changed: {original}")
            require(row["answer"] == "sat" and row["solver_exit"] == "0" and
                    row["validator"] == "dolmen", "invalid replay answer or validator")
            stem = f'{i:05d}-{original.name.replace(chr(92), "_")}'
            problem = dest / "models" / (stem + ".smt2")
            model = dest / "models" / (stem + ".rsmt2")
            hashes = {"input": digest(problem), "model": digest(model)}
            if row["outcome"] == "pass":
                require(row["dolmen_exit"] == "0", "nonzero direct validator exit")
                counts["direct_dolmen_pass"] += 1
            else:
                require(row["outcome"] == "input-nonconforming", "unresolved validation")
                key = (str(summary.resolve()), row["file"])
                exception = exceptions[key]
                require(exception["exit"] == 0 and exception["classification"] ==
                        "model-pass-with-wider-logic", "failed wider-logic validation")
                require(exception["input_sha256"] == hashes["input"] and
                        exception["model_sha256"] == hashes["model"], "triage model changed")
                require(exception["command"] == [
                    "contrib/local/bin/dolmen", "--check-model=true", "-r",
                    str(model.resolve()), "--strict=false", "--force-smtlib2-logic=QF_NRA",
                    "--size=2G", str(problem.resolve())], "triage invocation mismatch")
                require("Forbidden expression in linear arithmetic" in
                        (dest / "models" / (stem + ".dolmen.stderr")).read_text(),
                        "different validator rejection")
                counts["wider_logic_pass"] += 1
                used_exceptions.add(key)
            evidence.append({"parent": str(parent.relative_to(root)),
                             "file": selected[i]["file"],
                             "source_sha256": meta["inputs"][selected[i]["file"]],
                             "adapted_sha256": hashes["input"], "model_sha256": hashes["model"],
                             "outcome": row["outcome"],
                             "coverage_sha256": digest(dest / "coverage.json")})
        require(report["status"] == ("incomplete" if
                any(r["outcome"] != "pass" for r in validated) else "complete"),
                "unexpected coverage status")
        print(group, parent.name, len(expected), flush=True)
    expected_exceptions = {key for key in exceptions if any(
        str(parent.resolve()) + "/" in key[0]
        for _, parent in all_parents[part::parts])}
    require(used_exceptions == expected_exceptions, "unjoined triage rows")
    output.mkdir(parents=True, exist_ok=True)
    (output / "rows.json").write_text(json.dumps(evidence, indent=2) + "\n")
    totals = sum(groups.values(), Counter())
    require(totals["expected_sat"] == totals["direct_dolmen_pass"] +
            totals["wider_logic_pass"], "uncovered SAT measurement")
    result = {"status": "audited-with-explicit-validator-syntax-exceptions",
              "candidate_sha256": CANDIDATE, "dolmen_sha256": DOLMEN,
              "part": part, "parts": parts, "total_parent_shards": len(all_parents),
              "groups": groups, "totals": totals,
              "rows_sha256": digest(output / "rows.json"),
              "triage_sha256": digest(triage_path),
              "invalid_models": 0, "changed_answers": 0, "missing_rows": 0,
              "note": "Saved evidence audit; no solver execution. Wider-logic checks "
                      "remain distinct from direct acceptance under the declared logic. "
                      "Lost-case reruns and generated/session gates have separate audits."}
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)


def collect(output, parts):
    reports = [read(output / f"part-{i}/summary.json") for i in range(parts)]
    groups = {}
    for i, report in enumerate(reports):
        require(report["part"] == i and report["parts"] == parts,
                "partition identity mismatch")
        require(report["candidate_sha256"] == CANDIDATE and
                report["dolmen_sha256"] == DOLMEN, "partition binary mismatch")
        require(digest(output / f"part-{i}/rows.json") == report["rows_sha256"],
                "partition evidence changed")
        for group, counts in report["groups"].items():
            groups.setdefault(group, Counter()).update(counts)
    totals = sum(groups.values(), Counter())
    require(all(totals["shards"] == r["total_parent_shards"] for r in reports),
            "incomplete partition coverage")
    require(totals["expected_sat"] == totals["direct_dolmen_pass"] +
            totals["wider_logic_pass"] and totals["wider_logic_pass"] == 20,
            "incomplete SAT coverage")
    result = {"status": "audited-with-explicit-validator-syntax-exceptions",
              "groups": groups, "totals": totals,
              "candidate_sha256": CANDIDATE, "dolmen_sha256": DOLMEN,
              "invalid_models": 0, "changed_answers": 0, "missing_rows": 0,
              "parts": {str(i): digest(output / f"part-{i}/summary.json")
                        for i in range(parts)},
              "note": reports[0]["note"]}
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--part", type=int, default=0)
    parser.add_argument("--parts", type=int, default=1)
    parser.add_argument("--collect", action="store_true")
    args = parser.parse_args()
    require(0 <= args.part < args.parts, "invalid partition")
    if args.collect:
        collect(args.out.resolve(), args.parts)
    else:
        audit(args.results.resolve(), args.out.resolve(), args.part, args.parts)


if __name__ == "__main__":
    main()
