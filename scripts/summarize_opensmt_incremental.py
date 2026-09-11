#!/usr/bin/env python3
"""Compare additional OpenSMT stdin timings with preserved native UF/LRA runs."""

import argparse
import json
import statistics
from pathlib import Path

from summarize_native_uf import group, load_rows


def checked_rows(root, count, allow_errors=False):
    rows = load_rows(root)
    if set(rows) != set(range(count)):
        raise ValueError(f"incomplete/extra session coverage under {root}")
    rejected = {"failure", "wrong-answer"} | (set() if allow_errors else {"error"})
    if any(r["outcome"] in rejected for r in rows.values()):
        raise ValueError(f"failed timing rows under {root}")
    return rows


def compare(left, right):
    disagreements = []
    ratios = []
    for index in sorted(left.keys() & right.keys()):
        a, b = left[index], right[index]
        if a["file"] != b["file"]:
            raise ValueError("session identity mismatch")
        for query, (x, y) in enumerate(zip(a["answers"], b["answers"])):
            if x in {"sat", "unsat"} and y in {"sat", "unsat"} and x != y:
                disagreements.append({"index": index, "query": query, "answers": [x, y]})
        if a["outcome"] == b["outcome"] == "complete":
            if len(a["answers"]) != len(b["answers"]):
                raise ValueError("complete sessions have different query counts")
            ratios.append(a["seconds"] / b["seconds"])
    return {
        "disagreements": disagreements,
        "mutually_complete_sessions": len(ratios),
        "median_left_over_right_seconds": statistics.median(ratios) if ratios else None,
        "only_left_complete": sorted(i for i in left.keys() & right.keys()
                                     if left[i]["outcome"] == "complete"
                                     and right[i]["outcome"] != "complete"),
        "only_right_complete": sorted(i for i in left.keys() & right.keys()
                                      if right[i]["outcome"] == "complete"
                                      and left[i]["outcome"] != "complete"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--opensmt", type=Path, required=True)
    parser.add_argument("--uf-candidate", type=Path, required=True)
    parser.add_argument("--uf-prior", type=Path, required=True)
    parser.add_argument("--lra-candidate", type=Path, required=True)
    parser.add_argument("--lra-prior", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = {"opensmt": checked_rows(args.opensmt / "uf", 1778, allow_errors=True),
            "primo-assumptions-7ac555c": checked_rows(args.uf_candidate, 1778)}
    for label in ("rebuild", "assumptions", "z3", "cvc5"):
        data[label] = checked_rows(args.uf_prior / label, 1778)
    uf = {}
    for label, rows in data.items():
        uf[label] = {"all": group(list(rows.values())), "families": {}}
        families = sorted({r["file"].split("/")[2] for r in rows.values()})
        for family in families:
            uf[label]["families"][family] = group(
                [r for r in rows.values() if r["file"].split("/")[2] == family])
            uf[label]["families"][family]["versus_opensmt"] = compare(
                {i: r for i, r in rows.items() if r["file"].split("/")[2] == family},
                {i: r for i, r in data["opensmt"].items()
                 if r["file"].split("/")[2] == family})
        uf[label]["versus_opensmt"] = compare(rows, data["opensmt"])
    lra = checked_rows(args.opensmt / "lra", 2, allow_errors=True)
    candidate = json.loads(args.lra_candidate.read_text())
    prior = json.loads(args.lra_prior.read_text())
    result = {
        "uf": uf,
        "lra": {"opensmt": {"all": group(list(lra.values())), "rows": list(lra.values())},
                "recorded_primo": candidate["native_comparison"],
                "recorded_references": prior["aggregate"]},
        "limitations": [
            "Reference rows are answer comparisons, not independent OpenSMT model validation.",
            "Primo UF model qualification remains the separate recorded 7ac555c run.",
            "Incomplete sessions receive twice the whole-session limit, regardless of partial progress.",
            "Primo has a 10000 theory-conflict/query limit; external references have no matching cap.",
            "Reused runs are not contemporaneous paired measurements.",
        ],
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    return int(any(row["versus_opensmt"]["disagreements"] for row in uf.values()))


if __name__ == "__main__":
    raise SystemExit(main())
