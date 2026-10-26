#!/usr/bin/env python3
"""Check exact interval snapshots and summarize recorded LRA reason ancestry."""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from fractions import Fraction
from pathlib import Path


def number(pair):
    return tuple(Fraction(x) for x in pair)


def add(a, b):
    assert len(a) == len(b) == 2
    return tuple(x + y for x, y in zip(a, b))


def scale(a, c):
    return tuple(x * c for x in a)


def check_derived(event):
    """Recompute the row interval and ordered premise list from its snapshot."""
    upper = event["upper"]
    target = event["target"]
    reverse = target != event["row"]
    terms = event["terms"]
    coefficient = next(
        (Fraction(t["coeff"]) for t in terms if t["var"] == target), None
    )
    basic_upper = upper == (coefficient > 0) if reverse else upper
    total = (
        number(event["basic_bound"]["value"]) if reverse else (Fraction(0), Fraction(0))
    )
    reasons = [event["basic_bound"]["reason"]] if reverse else []
    for term in terms:
        if reverse and term["var"] == target:
            continue
        positive = Fraction(term["coeff"]) > 0
        need_upper = basic_upper != positive if reverse else upper == positive
        bound = term["upper" if need_upper else "lower"]
        assert bound is not None, "missing premise bound"
        contribution = scale(number(bound["value"]), Fraction(term["coeff"]))
        total = add(total, scale(contribution, -1) if reverse else contribution)
        reasons.append(bound["reason"])
    if reverse:
        total = scale(total, 1 / coefficient)
    assert total == number(event["value"]), "incorrect derived interval"
    assert reasons == event["reasons"], "incorrect derived reason selection"


def analyze(path):
    active = {}
    pending = {}
    catalog = defaultdict(list)
    seen = set()
    stronger_seen = set()
    unnamed_stronger_seen = set()
    counts = Counter()
    kinds = Counter()
    reasons_hist = Counter()
    depths = Counter()
    examples = []
    blocks = 0
    ends = 0
    capped = 0
    unknown = 0
    partial_line = False

    def expansion(reasons):
        nonlocal unknown
        leaves = Counter()
        depth = 0
        source_sets = {}
        for premise in reasons:
            lit = -premise
            proof = active.get(lit)
            if proof is None:
                unknown += 1
                proof = (Counter({lit: 1}), 0)
            source_sets[premise] = set(proof[0])
            leaves.update(proof[0])
            depth = max(depth, proof[1])
        sharing = sum(len(v) for v in source_sets.values()) - len(
            set().union(*source_sets.values())
        )
        return leaves, depth + 1, sharing

    with path.open() as stream:
        for line in stream:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                if line.endswith("\n"):
                    raise
                partial_line = True
                break
            kind = event["event"]
            counts[kind] += 1
            if kind == "begin":
                blocks += 1
                active.clear()
                pending.clear()
                catalog.clear()
                seen.clear()
                stronger_seen.clear()
                unnamed_stronger_seen.clear()
            elif kind == "end":
                ends += 1
                capped += bool(event["limit_reached"])
            elif kind == "atom":
                catalog[event["var"], event["upper"]].append(
                    (event["lit"], number(event["value"]))
                )
            elif kind == "assignment":
                lit = event["lit"]
                active[lit] = pending.pop(lit, (Counter({lit: 1}), 0))
            elif kind == "backtrack":
                for lit in event["removed"]:
                    for polarity in (lit, -lit):
                        active.pop(polarity, None)
                        pending.pop(polarity, None)
            elif kind in ("propagate", "conflict"):
                leaves, depth, theory_sharing = expansion(event["reasons"])
                reasons_hist[len(set(event["reasons"]))] += 1
                depths[depth] += 1
                if kind == "propagate":
                    pending[event["lit"]] = (leaves, depth)
                    kinds[event["kind"]] += 1
                counts["theory_expansions_with_shared_leaves"] += theory_sharing > 0
                shared = sum(leaves.values()) - len(leaves)
                counts["expanded_leaf_occurrences"] += sum(leaves.values())
                counts["expanded_distinct_leaves"] += len(leaves)
                if shared:
                    counts["reasons_with_shared_leaves"] += 1
                    examples.append(
                        {
                            "seq": event["seq"],
                            "event": kind,
                            "theory_shared_leaves": theory_sharing,
                            "raw_distinct": len(set(event["reasons"])),
                            "expanded_occurrences": sum(leaves.values()),
                            "expanded_distinct": len(leaves),
                            "depth": depth,
                            "leaves": dict(sorted(leaves.items())),
                        }
                    )
            elif kind == "derived":
                check_derived(event)
                counts["checked_derived"] += 1
                value = number(event["value"])
                identity = (
                    event["target"],
                    event["upper"],
                    value,
                    tuple(event["reasons"]),
                )
                atoms = catalog[event["target"], event["upper"]]
                exact = any(v == value for _, v in atoms)
                entails = any(
                    value <= v if event["upper"] else value >= v for _, v in atoms
                )
                installed = event["installed"]
                improves = installed is None or (
                    value < number(installed["value"])
                    if event["upper"]
                    else value > number(installed["value"])
                )
                if improves and identity not in stronger_seen:
                    stronger_seen.add(identity)
                    counts["derived_stronger_than_installed"] += 1
                if improves and not exact and identity not in unnamed_stronger_seen:
                    unnamed_stronger_seen.add(identity)
                    counts["stronger_without_exact_atom"] += 1
                if identity not in seen:
                    seen.add(identity)
                    counts["distinct_derived"] += 1
                    counts["derived_without_exact_atom"] += not exact
                    counts["derived_without_entailed_atom"] += not entails
    return {
        "trace": str(path),
        "blocks": blocks,
        "ends": ends,
        "coverage": "missing"
        if not blocks
        else ("prefix" if capped or blocks != ends or partial_line else "complete"),
        "capped_blocks": capped,
        "counts": dict(counts),
        "propagation_kinds": dict(kinds),
        "reason_width_histogram": dict(sorted(reasons_hist.items())),
        "theory_depth_histogram": dict(sorted(depths.items())),
        "unrecorded_antecedent_occurrences": unknown,
        "shared_examples": sorted(
            examples,
            key=lambda e: e["expanded_occurrences"] - e["expanded_distinct"],
            reverse=True,
        )[:5],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    results = [analyze(path) for path in args.traces]
    args.out.write_text(
        json.dumps({"schema_version": 1, "traces": results}, indent=2) + "\n"
    )
    print(
        json.dumps(
            {
                "traces": len(results),
                "checked_derived": sum(
                    r["counts"].get("checked_derived", 0) for r in results
                ),
            }
        )
    )


if __name__ == "__main__":
    main()
