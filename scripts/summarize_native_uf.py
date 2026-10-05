#!/usr/bin/env python3
"""Join native UF whole-session timings and separate model qualification.

Keep incomplete sessions out of paired speed ratios, retain partial query counts,
and require exact timed/replayed answer identity for model qualification.
"""

import argparse
import itertools
import json
import math
import statistics
from collections import Counter
from pathlib import Path

LABELS = ("rebuild", "assumptions", "z3", "cvc5")


def load_rows(root):
    rows = {}
    for path in sorted(root.glob("shard-*/summary.json")):
        for row in json.loads(path.read_text()):
            index = row["index"]
            if index in rows:
                raise ValueError(f"duplicate session {index} under {root}")
            rows[index] = row
    return rows


def group(rows):
    complete = [r for r in rows if r["outcome"] == "complete"]
    rss = [r["maxrss_kib"] for r in rows if r.get("maxrss_kib") is not None]
    return {
        "sessions": len(rows),
        "outcomes": dict(Counter(r["outcome"] for r in rows)),
        "completed_queries": sum(r["queries"] for r in complete),
        "partial_queries": sum(r["queries"] for r in rows if r["outcome"] != "complete"),
        "partial_definite_answers": sum(a in {"sat", "unsat"} for r in rows
                                        if r["outcome"] != "complete" for a in r["answers"]),
        "partial_unknown_answers": sum(a == "unknown" for r in rows
                                       if r["outcome"] != "complete" for a in r["answers"]),
        "expected_sat_models": sum(a == "sat" for r in complete for a in r["answers"]),
        "total_par2": sum(r["par2"] for r in rows),
        "mean_par2": statistics.mean(r["par2"] for r in rows) if rows else None,
        "median_complete_seconds": statistics.median(r["seconds"] for r in complete)
        if complete else None,
        "max_measured_rss_kib": max(rss, default=None),
        "rss_samples": len(rss),
    }


def qualify(timing, models):
    qualified = set()
    changed = []
    failed = []
    for index, row in models.items():
        if index not in timing:
            raise ValueError(f"validation without timing: {index}")
        if row["outcome"] == "pass":
            if [q["answer"] for q in row["queries"]] != timing[index]["answers"]:
                changed.append(index)
            elif timing[index]["outcome"] == "complete":
                qualified.add(index)
        elif row["outcome"] != "unqualified-timing":
            failed.append({"index": index, "outcome": row["outcome"],
                           "message": row.get("message")})
    return {
        "replayed_sessions": len(models),
        "qualified_complete_sessions": len(qualified),
        "qualified_queries": sum(len(models[i]["queries"]) for i in qualified),
        "sat_models": sum(q["answer"] == "sat" for i in qualified
                          for q in models[i]["queries"]),
        "changed_answers": sorted(changed),
        "failures": failed,
        "complete_without_qualification": sorted(i for i, row in timing.items()
                                               if row["outcome"] == "complete"
                                               and i not in qualified),
    }


def summarize(data, models, expected_sessions, families=None):
    if families is None:
        families = ("2018-Goel-hwbench", "20190906-CLEARSY")
    disagreements = []
    for left, right in itertools.combinations(LABELS, 2):
        for index in sorted(data[left].keys() & data[right].keys()):
            for query, (a, b) in enumerate(zip(data[left][index]["answers"],
                                               data[right][index]["answers"])):
                if a in {"sat", "unsat"} and b in {"sat", "unsat"} and a != b:
                    disagreements.append({"left": left, "right": right, "index": index,
                                          "query": query, "answers": [a, b]})
    result = {"profiles": {}, "reference_disagreements": disagreements}
    for label, rows in data.items():
        profile = group(list(rows.values()))
        profile["full_archive_timing"] = set(rows) == set(range(expected_sessions))
        profile["families"] = {
            family: group([r for r in rows.values() if family in Path(r["file"]).parts])
            for family in families
        }
        if label in models:
            profile["validation"] = qualify(rows, models[label])
            for family, family_result in profile["families"].items():
                selected = {i: r for i, r in rows.items() if family in Path(r["file"]).parts}
                family_result["validation"] = qualify(
                    selected, {i: r for i, r in models[label].items() if i in selected})
        result["profiles"][label] = profile
    old, new = data["rebuild"], data["assumptions"]
    common = sorted(old.keys() & new.keys())
    paired = [i for i in common if old[i]["outcome"] == new[i]["outcome"] == "complete"]
    ratios = [old[i]["seconds"] / new[i]["seconds"] for i in paired]
    result["assumptions_vs_rebuild"] = {
        "gains": [i for i in common if new[i]["outcome"] == "complete"
                  and old[i]["outcome"] != "complete"],
        "losses": [i for i in common if old[i]["outcome"] == "complete"
                   and new[i]["outcome"] != "complete"],
        "both_complete": len(paired),
        "median_speedup": statistics.median(ratios) if ratios else None,
        "geomean_speedup": math.exp(statistics.mean(math.log(r) for r in ratios))
        if ratios else None,
        "largest_regressions": [
            {"index": i, "file": old[i]["file"], "rebuild_seconds": old[i]["seconds"],
             "assumptions_seconds": new[i]["seconds"]}
            for i in sorted(paired, key=lambda i: new[i]["seconds"] - old[i]["seconds"],
                            reverse=True)[:20]
        ],
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timing", type=Path, required=True)
    parser.add_argument("--validation", type=Path)
    parser.add_argument("--expected-sessions", type=int, default=1778)
    parser.add_argument("--family", action="append",
                        help="Archive path component to group; repeat for multiple families. "
                             "Defaults to the two native UF families.")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = {label: load_rows(args.timing / label) for label in LABELS}
    models = {label: load_rows(args.validation / label) for label in LABELS[:2]} \
        if args.validation else {}
    result = summarize(data, models, args.expected_sessions, args.family)
    result["timing_root"] = str(args.timing)
    result["validation_root"] = str(args.validation) if args.validation else None
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    failures = any(profile.get("validation", {}).get("failures")
                   or profile.get("validation", {}).get("changed_answers")
                   for profile in result["profiles"].values())
    return 1 if result["reference_disagreements"] or failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
