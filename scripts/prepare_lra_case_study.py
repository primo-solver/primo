#!/usr/bin/env python3
"""Collect distinct generated conjunctions and freeze a diverse case-study sample.

This is deterministic experiment bookkeeping, not logical equivalence checking.
The selection procedure and its limits are defined in docs/papers/lra-call-ablation.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from collections import Counter, defaultdict
from pathlib import Path

from prepare_lra_call_replay import check_conjunction


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def conjunction_id(text):
    """Ignore comments, whitespace and the status annotation, retaining term order."""
    text = re.sub(r";[^\n]*", "", text)
    text = re.sub(r"\(set-info\s+:status\s+(sat|unsat|unknown)\s*\)", "", text)
    tokens = " ".join(re.findall(r"\(|\)|[^\s()]+", text))
    return hashlib.sha256(tokens.encode()).hexdigest()


def add_candidate(pool, source, expected, occurrence, retained=False):
    text = source.read_text()
    syntax = check_conjunction(text)
    if expected not in ("sat", "unsat", "unknown"):
        raise ValueError("invalid expected status")
    if syntax["expected"] != "unknown" and expected != syntax["expected"]:
        raise ValueError("contradictory declared status")
    identity = conjunction_id(text)
    raw = sha256(source)
    if identity not in pool:
        pool[identity] = {
            "conjunction_id": identity,
            "sha256": raw,
            "source": source,
            "syntax": syntax,
            "expected": expected,
            "retained": retained,
            "occurrences": [],
        }
    row = pool[identity]
    if row["expected"] != expected:
        if "unknown" not in (row["expected"], expected):
            raise ValueError(
                "contradictory expected statuses for identical conjunction"
            )
        if expected != "unknown":
            row["expected"] = expected
    # Prefer an existing frozen representative; otherwise break ties by raw hash.
    if (retained and not row["retained"]) or (
        retained == row["retained"] and raw < row["sha256"]
    ):
        row.update(sha256=raw, source=source, syntax=syntax)
    row["retained"] |= retained
    row["occurrences"].append({**occurrence, "raw_sha256": raw})


def collect(args):
    pool = {}
    for directory in args.include:
        metadata = directory / "occurrences.json"
        origins = (
            json.loads(metadata.read_text())
            if metadata.exists()
            else json.loads((directory / "metadata.json").read_text())["records"]
        )
        by_hash = {row["sha256"]: row for row in origins}
        truth_path = directory / "expected-statuses.json"
        truth = json.loads(truth_path.read_text()) if truth_path.exists() else {}
        for name in (directory / "manifest.txt").read_text().splitlines():
            source = directory / name
            raw = sha256(source)
            assert raw == source.stem
            expected = truth.get(
                name, check_conjunction(source.read_text())["expected"]
            )
            for occurrence in by_hash[raw]["occurrences"]:
                add_candidate(
                    pool,
                    source,
                    expected,
                    {
                        **occurrence,
                        "existing_dataset": directory.name,
                        "existing_file": name,
                    },
                    retained=True,
                )
    shards = sorted(args.extraction.glob("shard-*/summary.json"))
    assert shards, "no extraction shards"
    first = json.loads(shards[0].read_text())
    assert len(shards) == first["shards"], "missing extraction shards"
    expected_sources = {
        line.strip()
        for line in args.source_manifest.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    assert sha256(args.source_manifest) == first["manifest_sha256"]
    source_files = set()
    for path in shards:
        shard = json.loads(path.read_text())
        assert shard["complete"]
        for key in (
            "solver_sha256",
            "reference_sha256",
            "manifest_sha256",
            "version",
            "workflow_sha256",
            "solver_args",
            "timeout_seconds",
            "export_min_pivots",
            "export_limit",
        ):
            assert shard[key] == first[key], key
        for parent in shard["records"]:
            assert parent["file"] not in source_files
            source_files.add(parent["file"])
            assert not parent["wrong"]
            assert all(
                parent[k]["status"] in ("ok", "timeout")
                for k in ("native", "reference")
            )
            for call in parent["analysis"].get("exports", []):
                source = (
                    path.parent
                    / f"case-{parent['index']:04d}"
                    / "cubes"
                    / call["export"]
                )
                add_candidate(
                    pool,
                    source,
                    "unsat" if call["conflict"] else "sat",
                    {
                        "parent": parent["file"],
                        "parent_sha256": parent["input_sha256"],
                        "parent_coverage": parent["analysis"]["coverage"],
                        "call": call,
                        "extraction_shard": path.parent.name,
                        "extraction_case": f"case-{parent['index']:04d}",
                    },
                )
    assert source_files == expected_sources, (
        "source coverage differs from frozen manifest"
    )
    cubes = args.out / "cubes"
    cubes.mkdir(parents=True, exist_ok=False)
    records = []
    for identity, row in sorted(pool.items()):
        source = row.pop("source")
        name = f"cubes/{row['sha256']}.smt2"
        shutil.copyfile(source, args.out / name)
        row["file"] = name
        records.append(row)
    result = {
        "status": "candidate-pool-not-yet-fresh-qualified",
        "source_inputs": len(source_files),
        "native_version": first["version"],
        "binary_sha256": first["solver_sha256"],
        "source_manifest_sha256": first["manifest_sha256"],
        "source_timeout_seconds": first["timeout_seconds"],
        "solver_args": first["solver_args"],
        "workflow_sha256": first["workflow_sha256"],
        "export_min_pivots": first["export_min_pivots"],
        "export_limit": first["export_limit"],
        "deduplication": "Token order retained; comments, whitespace and status annotation ignored; not logical equivalence.",
        "candidates": len(records),
        "retained": sum(r["retained"] for r in records),
        "raw_occurrences": sum(len(r["occurrences"]) for r in records),
        "records": records,
    }
    (args.out / "pool.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "records"}))


def parent_name(row):
    return min(o["parent"] for o in row["occurrences"])


def warm_pivots(row):
    return max(
        (o.get("call", {}).get("pivots", 0) for o in row["occurrences"]), default=0
    )


def select_records(records, target, seed, max_parent, max_family):
    """Retain originals, then round-robin families/parents, alternating warm strata."""
    chosen = sorted(
        (r for r in records if r["retained"]), key=lambda r: r["conjunction_id"]
    )
    if len(chosen) > target:
        raise ValueError("target smaller than retained corpus")
    parents = Counter(parent_name(r) for r in chosen)
    families = Counter(parent_name(r).split("/")[0] for r in chosen)
    if any(n > max_parent for n in parents.values()) or any(
        n > max_family for n in families.values()
    ):
        raise ValueError("retained inputs exceed selection quota")
    buckets = defaultdict(list)
    for row in records:
        if not row["retained"]:
            buckets[parent_name(row)].append(row)
    for name, rows in buckets.items():
        # Interleave three higher-pivot candidates with one medium-pivot control.
        rank = lambda r: hashlib.sha256(
            f"{seed}:{r['conjunction_id']}".encode()
        ).hexdigest()
        high = sorted((r for r in rows if warm_pivots(r) >= 128), key=rank)
        medium = sorted((r for r in rows if warm_pivots(r) < 128), key=rank)
        ordered = []
        while high or medium:
            ordered.extend(high[:3])
            del high[:3]
            ordered.extend(medium[:1])
            del medium[:1]
        buckets[name] = ordered
    groups = defaultdict(list)
    for name in sorted(buckets):
        groups[name.split("/")[0]].append(name)
    while len(chosen) < target:
        progress = False
        for family in sorted(groups):
            if families[family] >= max_family:
                continue
            # One eligible parent per family per round; prefer fewer already selected.
            eligible = [
                p for p in groups[family] if buckets[p] and parents[p] < max_parent
            ]
            if not eligible:
                continue
            parent = min(eligible, key=lambda p: (parents[p], p))
            chosen.append(buckets[parent].pop(0))
            parents[parent] += 1
            families[family] += 1
            progress = True
            if len(chosen) == target:
                break
        if not progress:
            raise ValueError(
                f"only {len(chosen)} distinct candidates fit quotas; target {target}"
            )
    return chosen


def select(args):
    pool = json.loads((args.pool / "pool.json").read_text())
    selected = select_records(
        pool["records"],
        args.target,
        args.seed,
        args.max_per_parent,
        args.max_per_family,
    )
    (args.out / "cubes").mkdir(parents=True, exist_ok=False)
    selected.sort(key=lambda row: row["file"])
    for row in selected:
        shutil.copyfile(args.pool / row["file"], args.out / row["file"])
    (args.out / "manifest.txt").write_text("".join(r["file"] + "\n" for r in selected))
    (args.out / "expected-statuses.json").write_text(
        json.dumps({r["file"]: r["expected"] for r in selected}, indent=2) + "\n"
    )
    result = {k: v for k, v in pool.items() if k != "records"}
    result.update(
        status="frozen-selection-awaiting-fresh-qualification",
        target=args.target,
        seed=args.seed,
        max_per_parent=args.max_per_parent,
        max_per_family=args.max_per_family,
        selected=len(selected),
        selected_statuses=dict(Counter(r["expected"] for r in selected)),
        selected_families=dict(Counter(parent_name(r).split("/")[0] for r in selected)),
        records=selected,
    )
    (args.out / "selection.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "records"}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    c = modes.add_parser("collect")
    c.add_argument("--extraction", type=Path, required=True)
    c.add_argument("--source-manifest", type=Path, required=True)
    c.add_argument("--include", type=Path, action="append", default=[])
    c.add_argument("--out", type=Path, required=True)
    s = modes.add_parser("select")
    s.add_argument("--pool", type=Path, required=True)
    s.add_argument("--out", type=Path, required=True)
    s.add_argument("--target", type=int, default=1000)
    s.add_argument("--seed", type=int, default=20261030)
    s.add_argument("--max-per-parent", type=int, default=25)
    s.add_argument("--max-per-family", type=int, default=400)
    args = parser.parse_args()
    if args.mode == "collect":
        collect(args)
    else:
        if min(args.target, args.max_per_parent, args.max_per_family) < 1:
            parser.error("target and quotas must be positive")
        select(args)


if __name__ == "__main__":
    main()
