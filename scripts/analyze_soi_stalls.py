#!/usr/bin/env python3
"""Analyze existing SOI stall counters and bounded read-only GDB traces."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

from benchmark_model_replay import digest, write_json


def counter_analysis(summary):
    cases = summary["per_input"]
    common = set.intersection(
        *(
            {name for name, row in rows.items() if row["stable"]}
            for rows in cases.values()
        )
    )
    results = {}
    for fixed in (0, 1):
        policies = {}
        for policy in ("current-dantzig", "current-sparse", "suspended-combined"):
            rows = cases[f"{policy}-g1-f{fixed}"]
            counts = Counter()
            families = defaultdict(Counter)
            details = []
            for name in sorted(common):
                row = rows[name]
                work = row["work"]
                if work[":lra.online-check-calls"] != 1:
                    raise ValueError("stall inference requires one check per input")
                fallback = work[":lra.bland-fallbacks"]
                if fallback not in (0, 1):
                    raise ValueError("expected binary median fallback indicator")
                counts["fallback_inputs"] += int(fallback)
                counts["pivots"] += work[":lra.pivots"]
                families[row["family"]]["inputs"] += 1
                families[row["family"]]["fallback_inputs"] += int(fallback)
                if policy == "suspended-combined" and fallback:
                    steps = work[":lra.soi-selected-steps"]
                    prefix = work[":lra.soi-prefix-pivots"]
                    cap = 4 * work[":lra.tableau-vars"] + 64
                    counts["below_common_cap"] += int(steps <= cap)
                    counts["exactly_eight_zero_steps"] += int(
                        work[":lra.soi-zero-progress-steps"] == 8
                    )
                    counts["exactly_eight_nonpivot_steps"] += int(steps - prefix == 8)
                    counts["no_prefix_pivots"] += int(prefix == 0)
                    details.append(
                        {
                            "file": name,
                            "family": row["family"],
                            "selected_steps": steps,
                            "prefix_pivots": prefix,
                            "zero_steps": work[":lra.soi-zero-progress-steps"],
                            "cap": cap,
                        }
                    )
            policies[policy] = {
                "counts": dict(counts),
                "families": {k: dict(v) for k, v in sorted(families.items())},
                "fallback_details": details,
            }
        results[str(fixed)] = policies
    return {"common_stable_inputs": len(common), "settings": results}


def trace_analysis(root):
    results = {}
    for path in sorted(root.glob("*.json")):
        trace = json.loads(path.read_text())
        events = trace["events"]
        if trace["exit_code"] != 0 or any("error" in event for event in events):
            raise ValueError(f"incomplete or unreadable trace: {path}")
        if sum(event["event"] == "suspend_soi_for_fallback" for event in events) != 1:
            raise ValueError("expected one completed prefix and fallback boundary")
        updates = [event for event in events if event["event"] == "update"]
        tail = updates[-8:]
        same_var = len({event["var"] for event in tail}) == 1
        no_pivots = len({event["prefix_pivots"] for event in tail}) == 1
        repeated_noop = (
            len(tail) == 8
            and same_var
            and no_pivots
            and all(event["unchanged"] is True for event in tail)
            and all(
                event["selection"]["entering"] == event["selection"]["leaving"]
                and event["selection"]["delta"] == [[0, 1], [0, 1]]
                for event in tail
            )
            and tail[-1]["selected_steps"] - tail[0]["selected_steps"] == 7
        )
        answers = re.findall(
            r"^(sat|unsat)$", path.with_suffix(".log").read_text(), re.MULTILINE
        )
        if len(answers) != 1:
            raise ValueError("missing unique terminal solver answer")
        results[path.stem] = {
            "sha256": digest(path),
            "answer": answers[0],
            "events": events,
            "updates": len(updates),
            "last_eight_distinct_bases": len({event["basis_sha256"] for event in tail}),
            "last_eight_same_variable_noops_without_pivots": repeated_noop,
        }
    if len(results) != 3:
        raise ValueError("expected all three trace inputs")
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--qualified-report", type=Path, required=True)
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.qualified_report.read_text())
    if digest(args.summary) != report["raw_sha256"]:
        raise ValueError("summary differs from qualified source")
    summary = json.loads(args.summary.read_text())
    traces = trace_analysis(args.traces)
    for name, trace in traces.items():
        original = summary["per_input"]["suspended-combined-g1-f0"][
            f"cubes/{name}.smt2"
        ]
        if not original["stable"] or set(original["outcomes"]) != {trace["answer"]}:
            raise ValueError("debug trace answer differs from qualified timing runs")
    write_json(
        args.out,
        {
            "schema": 1,
            "analysis_sha256": digest(Path(__file__)),
            "summary_sha256": digest(args.summary),
            "trace_binary_sha256": (args.traces / "binary.sha256")
            .read_text()
            .split()[0],
            "trace_version": (args.traces / "version.txt").read_text(),
            "callback_sha256": digest(
                Path("scripts/qualification/soi_stalls/trace.py")
            ),
            "counter_scope": "Per-input medians of three unprofiled timing repetitions; common stable intersection across all 24 configurations. These are distinct from the one-repetition call-profile aggregates.",
            "trace_scope": "Three selected fixed-off inputs on the existing Debug/ASAN build of 8faa775, under GDB; no timing claim or inference of prevalence from this selected sample. Callbacks only read inferior state.",
            "counters": counter_analysis(summary),
            "traces": traces,
        },
    )


if __name__ == "__main__":
    main()
