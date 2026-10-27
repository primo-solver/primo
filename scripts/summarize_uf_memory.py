#!/usr/bin/env python3
"""Audit and summarize the frozen UF rollback measurement; never run a solver."""

import argparse
import hashlib
import json
import re
import statistics
from pathlib import Path

METRICS = (
    "wall_seconds", "user_seconds", "system_seconds", "max_rss_kib",
    "minor_faults", "major_faults",
)
COUNTERS = (
    ":sat.conflicts", ":sat.decisions", ":quant.instances", ":quant.pool-terms",
    ":euf.incremental.terms", ":euf.incremental.backtracks",
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(plan_path, results):
    plan = json.loads(plan_path.read_text())
    cases = []
    fingerprints = None
    for index, case in enumerate(plan["cases"]):
        directory = results / f"case-{index}"
        metadata = json.loads((directory / "metadata.json").read_text())
        complete = json.loads((directory / "complete.json").read_text())
        rows = json.loads((directory / "summary.json").read_text())
        expected_pairs = {(p, r) for p in ("baseline", "fixed")
                          for r in range(plan["repetitions"])}
        if (metadata["case"] != case or metadata["plan_sha256"] != sha(plan_path)
                or complete["runs"] != len(expected_pairs) or len(rows) != len(expected_pairs)
                or {(r["profile"], r["repetition"]) for r in rows} != expected_pairs):
            raise ValueError(f"Incomplete or changed case {index}")
        if fingerprints is None:
            fingerprints = metadata["binary_sha256"]
        if (metadata["binary_sha256"] != fingerprints
                or complete["binary_sha256"] != fingerprints
                or metadata["partition"] != "compute" or len(metadata["affinity"]) != 1
                or metadata["args"] != plan["solver_args"]
                or metadata["timeout_seconds"] != plan["timeout_seconds"]):
            raise ValueError(f"Execution provenance mismatch in case {index}")
        for row in rows:
            limit = plan["as_limit_bytes"]
            observed = row["observed_limits"] or ""
            if (row["as_limit_bytes"] != limit
                    or not re.search(rf"Max address space\s+{limit}\s+{limit}\s+bytes", observed)
                    or row != {"profile": row["profile"], "repetition": row["repetition"],
                               **json.loads((directory / f"round-{row['repetition']}-{row['profile']}"
                                             / "result.json").read_text())}):
                raise ValueError(f"Resource/result mismatch in case {index}")
            if row["status"] == "solved" and (
                    row["exit_code"] != 0 or row["answer"] != case["expected"]):
                raise ValueError(f"Answer mismatch in case {index}")
        profiles = {}
        for profile in ("baseline", "fixed"):
            selected = [r for r in rows if r["profile"] == profile]
            profiles[profile] = {
                "statuses": [r["status"] for r in selected],
                "answers": [r["answer"] for r in selected],
                "metrics": {key: {"median": statistics.median(r[key] for r in selected),
                                  "min": min(r[key] for r in selected),
                                  "max": max(r[key] for r in selected)} for key in METRICS},
                "sampled_vm_peak_kib": [r["sampled_proc_maxima_kib"]["VmPeak"] for r in selected],
                "counters": {key: [r["statistics"].get(key) for r in selected] for key in COUNTERS},
            }
        cases.append({"logic": case["logic"], "file": case["file"], "sha256": case["sha256"],
                      "cpu_models": metadata["cpu_models"], "profiles": profiles})
    return {
        "id": plan["id"], "job": results.name,
        "native_source_git": {"baseline": "0d5970400743e1403b7160b7f0126de6ececa8fe",
                              "fixed": "ed8c227aea49925ed8117f656bf44f14a030a4b9"},
        "binary_sha256": fingerprints, "plan_sha256": sha(plan_path),
        "executed_sbatch_sha256": sha(Path("scripts/qualification/uf_memory/paired.sbatch")),
        "timeout_seconds": plan["timeout_seconds"], "as_limit_bytes": plan["as_limit_bytes"],
        "solver_args": plan["solver_args"], "repetitions": plan["repetitions"],
        "build": "Release, /usr/bin/c++, -O3 -DNDEBUG, identical explicit C++ header paths, no sanitizers; CaDiCaL 3.0.0-7b99c07",
        "execution": "CPU compute partition; requested 1 CPU, scheduler allocated 2 logical CPUs, pinned controller/children to 1; 10G reservation, 8GiB hard child RLIMIT_AS",
        "scope": "Six selected diagnostics, not a full-corpus evaluation. Failed-run time is time to failure. RSS is wait4 process peak; sampled VmPeak is a lower bound. Counters are cumulative, not resident storage. Quantified UNSAT EUF counters can remain unflushed; failed runs print no final counters.",
        "cases": cases,
        "raw_artifact_sha256": {str(p.relative_to(results)): sha(p)
                                for p in sorted(results.rglob("*")) if p.is_file()},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = summarize(args.plan, args.results)
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
