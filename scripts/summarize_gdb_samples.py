#!/usr/bin/env python3
"""Summarize Primo GDB stacks by disjoint phase, preserving raw per-run counts."""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path


def phase(frames: list[str]) -> str:
    joined = "\n".join(frames)
    if "Frontend::~Frontend" in joined:
        return "frontend_cleanup"
    if "flatten_associative_booleans" in joined:
        return "boolean_flattening"
    if "prepare_and_execute_preprocessing_strategy" in joined:
        return "other_preprocessing"
    if "BooleanClausifier" in joined:
        return "clausification"
    if "CaDiCaL::Internal::preprocess" in joined:
        return "sat_preprocessing"
    if "CadicalBackend::solve" in joined:
        return "other_sat_solve"
    return "other"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    runs = {}
    for path in sorted(args.root.glob("*/summary.json")):
        identity = json.loads(path.read_text())
        if identity["status"] != "complete":
            raise ValueError(f"incomplete profile: {path}")
        samples = [json.loads(line) for line in
                   (path.parent / "samples.jsonl").read_text().splitlines()]
        if len(samples) != identity["samples"] or not samples:
            raise ValueError(f"missing samples: {path}")
        phases = collections.Counter(phase(row["frames"]) for row in samples)
        leaf = collections.Counter(row["frames"][0] if row["frames"] else "unknown"
                                   for row in samples)
        inclusive = collections.Counter(name for row in samples for name in set(row["frames"]))
        allocator = sum(any(name.startswith(("_int_malloc", "_int_free", "malloc", "free",
                                            "calloc", "realloc", "unlink_chunk", "__libc_malloc",
                                            "__libc_free")) for name in row["frames"])
                        for row in samples)
        statistics = {}
        answers = []
        for line in (path.parent / "gdb-mi.log").read_text().splitlines():
            if line in ("sat", "unsat", "unknown"):
                answers.append(line)
            if line.startswith(":") and len(line.split()) == 2:
                key, value = line.split()
                statistics[key] = value
        runs[path.parent.name] = {
            "identity": identity, "samples": len(samples), "phase_samples": dict(phases),
            "phase_percent": {key: 100 * count / len(samples) for key, count in phases.items()},
            "allocator_samples": allocator,
            "allocator_percent": 100 * allocator / len(samples),
            "top_leaf": leaf.most_common(20), "inclusive": dict(inclusive),
            "answers": answers, "statistics": statistics,
        }
    if not runs:
        raise ValueError("no profiles found")
    print(json.dumps({
        "runs": runs,
        "note": "Phase categories are first matching stack predicates, not timer regions. "
                "Allocator share overlaps phases. Wall-clock samples are perturbed by GDB; "
                "do not interpret profiled duration as benchmark time.",
    }, indent=2))


if __name__ == "__main__":
    main()
