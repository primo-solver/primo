#!/usr/bin/env python3
"""Audit and summarize the prescribed simplex factorial without dropping failures.

The per-input process score is the median of three PAR2 contributions, with
unsolved processes charged twice the limit. A separate mean per-repeat PAR2
retains intermittent failures that the median can hide. Ratios use only pairs
solved in every repetition; their uncertainty uses resampled counting parents.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import statistics
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

from benchmark_model_replay import digest, write_json
from run_lra_simplex_ablation import (
    check_run,
    decisive,
    model_groups,
    read_rows,
    schedule,
)
from validate_qfuf_models_with_dolmen import model_problem_text


def quantiles(values):
    ordered = sorted(values)
    return {
        label: ordered[max(0, math.ceil(len(ordered) * q) - 1)] if ordered else None
        for label, q in [
            ("min", 0),
            ("median", 0.5),
            ("p90", 0.9),
            ("p99", 0.99),
            ("max", 1),
        ]
    }


def clustered_ratio(pairs, seed, samples=2000):
    """pairs contains (parent, numerator/denominator), one per conjunction."""
    if not pairs:
        return {"pairs": 0, "parents": 0, "geomean": None, "ci95": None}
    groups = defaultdict(list)
    for parent, ratio in pairs:
        if ratio <= 0:
            raise ValueError("ratio requires positive observations")
        groups[parent].append(math.log(ratio))
    blocks = [(sum(v), len(v)) for _, v in sorted(groups.items())]
    observed = math.exp(sum(s for s, _ in blocks) / sum(n for _, n in blocks))
    rng = random.Random(seed)
    estimates = []
    for _ in range(samples):
        chosen = rng.choices(blocks, k=len(blocks))
        estimates.append(
            math.exp(sum(s for s, _ in chosen) / sum(n for _, n in chosen))
        )
    estimates.sort()
    return {
        "pairs": len(pairs),
        "parents": len(blocks),
        "geomean": observed,
        "ci95": [estimates[int(samples * 0.025)], estimates[int(samples * 0.975) - 1]],
        "bootstrap_samples": samples,
        "seed": seed,
    }


def stratum(origin):
    if origin["fresh_coverage"] != "complete" or origin["fresh_completed_checks"] != 1:
        return "censored"
    pivots = origin["fresh_completed_pivots"]
    return (
        "lt32"
        if pivots < 32
        else "32-127"
        if pivots < 128
        else "128-1023"
        if pivots < 1024
        else "ge1024"
    )


def outcome(run):
    return decisive(run) or (run["answer"] if run["status"] == "ok" else run["status"])


def compact(rows, origin, timeout):
    complete = [r for r in rows if decisive(r["run"])]
    single = [
        r
        for r in complete
        if r["analysis"]["coverage"] == "complete" and r["analysis"]["calls"] == 1
    ]
    successful_trace = [
        r for r in complete if r["analysis"]["coverage"] in ("complete", "no-checks")
    ]
    process = [float(r["run"]["seconds"]) for r in rows]
    penalties = [
        float(r["run"]["seconds"]) if decisive(r["run"]) else 2 * timeout for r in rows
    ]
    work = {}
    for key, stat in [
        ("row_updates", ":lra.pivot-row-updates"),
        ("term_ops", ":lra.pivot-term-ops"),
        ("fallbacks", ":lra.bland-fallbacks"),
        ("elimination_pivots", ":lra.elimination-pivots"),
        ("rational_promotions", ":rational.promotions"),
    ]:
        work[key] = (
            statistics.median([int(r["run"]["stats"].get(stat, 0)) for r in complete])
            if len(complete) == len(rows)
            else None
        )
    return {
        "file": rows[0]["file"],
        "configuration": rows[0]["configuration"],
        "expected": origin["expected"],
        "parent": origin["counting_parent"],
        "family": origin["counting_parent"].split("/")[0],
        "stratum": stratum(origin),
        "successful_repeats": len(complete),
        "outcomes": dict(Counter(outcome(r["run"]) for r in rows)),
        "process_seconds": statistics.median(process)
        if len(complete) == len(rows)
        else None,
        "median_par2_seconds": statistics.median(penalties),
        "mean_par2_seconds": statistics.mean(penalties),
        "single_check_repeats": len(single),
        "check_ns": statistics.median([r["analysis"]["total_check_ns"] for r in single])
        if len(single) == len(rows)
        else None,
        "pivots": statistics.median(
            [r["analysis"]["total_pivots"] for r in successful_trace]
        )
        if len(successful_trace) == len(rows)
        else None,
        "check_counts": [r["analysis"].get("calls", 0) for r in rows],
        "checker_dimensions": [r["analysis"].get("checkers", []) for r in rows],
        **work,
    }


def summarize_group(values):
    outcomes = Counter()
    for v in values:
        outcomes.update(v["outcomes"])
    return {
        "inputs": len(values),
        "timing_rows": sum(outcomes.values()),
        "outcomes": dict(outcomes),
        "solved_all_repeats": sum(v["successful_repeats"] == 3 for v in values),
        "solved_any_repeat": sum(v["successful_repeats"] > 0 for v in values),
        "intermittent_solve": sum(0 < v["successful_repeats"] < 3 for v in values),
        "mean_repeat_par2_seconds": sum(v["mean_par2_seconds"] for v in values),
        "sum_input_median_par2_seconds": sum(v["median_par2_seconds"] for v in values),
        "process_seconds_complete_triplets": quantiles(
            [v["process_seconds"] for v in values if v["process_seconds"] is not None]
        ),
        "check_ns_complete_single_check_triplets": quantiles(
            [v["check_ns"] for v in values if v["check_ns"] is not None]
        ),
        "pivots_complete_triplets": quantiles(
            [v["pivots"] for v in values if v["pivots"] is not None]
        ),
        "single_check_triplets": sum(v["single_check_repeats"] == 3 for v in values),
        "no_check_triplets": sum(
            v["successful_repeats"] == 3 and v["check_counts"] == [0, 0, 0]
            for v in values
        ),
        "fallback_triplets": sum(
            v["fallbacks"] is not None and v["fallbacks"] > 0 for v in values
        ),
    }


def comparison(numerator, denominator, seed):
    shared = sorted(set(numerator) & set(denominator))
    result = {}
    for metric in ("process_seconds", "check_ns", "pivots", "row_updates", "term_ops"):
        pairs = [
            (numerator[n]["parent"], numerator[n][metric] / denominator[n][metric])
            for n in shared
            if numerator[n][metric] is not None
            and denominator[n][metric] is not None
            and numerator[n][metric] > 0
            and denominator[n][metric] > 0
        ]
        result[metric] = clustered_ratio(pairs, seed)
        result[metric]["zero_value_pairs_excluded"] = sum(
            numerator[n][metric] is not None
            and denominator[n][metric] is not None
            and (numerator[n][metric] == 0 or denominator[n][metric] == 0)
            for n in shared
        )
    result["stable_solve_gains"] = [
        n
        for n in shared
        if denominator[n]["successful_repeats"] == 3
        and numerator[n]["successful_repeats"] == 0
    ]
    result["stable_solve_losses"] = [
        n
        for n in shared
        if numerator[n]["successful_repeats"] == 3
        and denominator[n]["successful_repeats"] == 0
    ]
    return result


def load_audit(args, plan, origins):
    indexed = defaultdict(list)
    metas = []
    model_metas = []
    allkeys = set()
    passes = Counter()
    native_id = None
    for shard in range(args.shards):
        timing = args.results / "timing" / f"shard-{shard}"
        meta = json.loads((timing / "metadata.json").read_text())
        if (
            not meta["complete"]
            or meta["shard"] != shard
            or meta["shards"] != args.shards
        ):
            raise ValueError("incomplete/incorrect timing shard")
        if native_id is None:
            native_id = meta["solver"]
        for field, value in [
            ("solver", native_id),
            ("plan_sha256", digest(args.plan)),
            ("manifest_sha256", plan["manifest_sha256"]),
            ("expected_statuses_sha256", plan["expected_statuses_sha256"]),
        ]:
            if meta[field] != value:
                raise ValueError(f"shard identity mismatch: {field}")
        if native_id["sha256"] != plan["solver_sha256"]:
            raise ValueError("wrong binary")
        if metas and meta["workflow_sha256"] != metas[0]["workflow_sha256"]:
            raise ValueError("workflow changed between shards")
        metas.append(meta)
        rows = read_rows(timing / "rows.jsonl")
        scheduled = list(schedule(plan, origins, shard, args.shards))
        if len(rows) != len(scheduled) or len(rows) != meta["rows"]:
            raise ValueError("missing timing rows")
        for row, (name, repeat, cfg) in zip(rows, scheduled, strict=True):
            key = f"{Path(name).stem}-r{repeat}-{cfg['id']}"
            if (row["key"], row["file"], row["repeat"], row["configuration"]) != (
                key,
                name,
                repeat,
                cfg["id"],
            ) or key in allkeys:
                raise ValueError("duplicate/changed timing identity or order")
            if (
                row["input_sha256"] != origins[name]["sha256"]
                or row["expected"] != origins[name]["expected"]
            ):
                raise ValueError("input identity changed")
            command = row["command"]
            if (
                command[1:-3] != [*plan["common_args"], *cfg["args"]]
                or command[-3] != "--lra-call-profile"
            ):
                raise ValueError("timing numerical arguments differ from plan")
            if Path(command[-1]).name != Path(name).name:
                raise ValueError("timing command input mismatch")
            check_run(row["run"], row["expected"])
            allkeys.add(key)
            indexed[(cfg["id"], name)].append(row)
            if args.verify_artifacts:
                trace = timing / key / "calls.jsonl"
                if (digest(trace) if trace.exists() else None) != row["trace_sha256"]:
                    raise ValueError("timing trace changed")
        models = args.results / "models" / f"shard-{shard}"
        model_meta = json.loads((models / "metadata.json").read_text())
        if not model_meta["complete"]:
            raise ValueError("incomplete model shard")
        for field in (
            "solver",
            "plan_sha256",
            "manifest_sha256",
            "expected_statuses_sha256",
            "workflow_sha256",
        ):
            if model_meta[field] != meta[field]:
                raise ValueError("model provenance changed")
        model_metas.append(model_meta)
        joins = (
            json.loads((models / "joins.json").read_text())
            if model_meta["passed"]
            else []
        )
        expected_sat = {r["key"] for r in rows if decisive(r["run"]) == "sat"}
        if (
            len(joins) != model_meta["passed"]
            or len(joins) != len(expected_sat)
            or {j["key"] for j in joins} != expected_sat
        ):
            raise ValueError("missing/duplicate/extra model joins")
        if any((j["outcome"], j["validator"]) != ("pass", "dolmen") for j in joins):
            raise ValueError("invalid model join")
        joined = {j["key"]: j for j in joins}
        for row in rows:
            if row["key"] in joined:
                passes[row["configuration"]] += 1
        if args.verify_artifacts:
            for (config, repeat), batch in model_groups(rows).items():
                target = models / f"{config}-r{repeat}"
                raw_meta = json.loads((target / "metadata.json").read_text())
                cfg = next(c for c in plan["configurations"] if c["id"] == config)
                if (
                    raw_meta["status"] != "complete"
                    or not raw_meta["expect_sat"]
                    or not raw_meta["require_dolmen_pass"]
                ):
                    raise ValueError("model validator phase incomplete")
                if raw_meta["solver_args"] != [
                    *plan["common_args"],
                    *cfg["args"],
                    "--check-model",
                ]:
                    raise ValueError("model numerical arguments changed")
                with (target / "summary.csv").open() as stream:
                    checks = list(csv.DictReader(stream))
                if len(checks) != len(batch):
                    raise ValueError("model raw summary missing rows")
                for i, (row, check) in enumerate(zip(batch, checks, strict=True)):
                    path = args.corpus / row["file"]
                    adapted = target / f"{i:05d}-{path.name}.smt2"
                    if digest(adapted) != joined[row["key"]][
                        "adapted_sha256"
                    ] or adapted.read_text() != model_problem_text(path.read_text()):
                        raise ValueError("adapted assertions changed")
                    if (
                        check["answer"],
                        check["outcome"],
                        check["validator"],
                        check["solver_exit"],
                        check["dolmen_exit"],
                    ) != ("sat", "pass", "dolmen", "0", "0"):
                        raise ValueError("model raw result failed")
    if max(datetime.fromisoformat(m["finished"]) for m in metas) > min(
        datetime.fromisoformat(m["started"]) for m in model_metas
    ):
        raise ValueError("model stage overlaps timing")
    if len({json.dumps(m["dolmen"], sort_keys=True) for m in model_metas}) != 1:
        raise ValueError("validator identity changed")
    if args.verify_artifacts:
        for name, origin in origins.items():
            if digest(args.corpus / name) != origin["sha256"]:
                raise ValueError("frozen input changed")
    return indexed, metas, model_metas, passes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("plan", "corpus", "results", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--shards", type=int, default=64)
    parser.add_argument("--verify-artifacts", action="store_true")
    args = parser.parse_args()
    plan = json.loads(args.plan.read_text())
    corpus = json.loads((args.corpus / "metadata.json").read_text())
    origins = {r["file"]: r for r in corpus["records"]}
    names = (args.corpus / "manifest.txt").read_text().splitlines()
    statuses = json.loads((args.corpus / "expected-statuses.json").read_text())
    if (
        len(names) != len(set(names))
        or len(names) != plan["inputs"]
        or set(names) != set(origins)
    ):
        raise ValueError("incomplete or duplicate frozen corpus")
    if (
        digest(args.corpus / "manifest.txt") != plan["manifest_sha256"]
        or digest(args.corpus / "expected-statuses.json")
        != plan["expected_statuses_sha256"]
    ):
        raise ValueError("frozen corpus metadata changed")
    if {n: r["expected"] for n, r in origins.items()} != statuses:
        raise ValueError("frozen labels changed")
    indexed, metas, model_metas, passes = load_audit(args, plan, origins)
    compacted = {
        key: compact(rows, origins[key[1]], plan["timeout_seconds"])
        for key, rows in indexed.items()
    }
    byconfig = {
        c["id"]: {
            name: compacted[c["id"], name]
            for name in origins
            if origins[name]["expected"] != "unknown"
        }
        for c in plan["configurations"]
    }
    report = {
        "status": "qualified-all-timing-rows-and-all-successful-sat-repetitions",
        "analysis_script_sha256": digest(Path(__file__)),
        "corpus_metadata_sha256": digest(args.corpus / "metadata.json"),
        "plan_sha256": digest(args.plan),
        "binary": metas[0]["solver"],
        "workflow_sha256": metas[0]["workflow_sha256"],
        "validator": model_metas[0]["dolmen"],
        "timing_rows": sum(len(v) for v in indexed.values()),
        "hard_failures": {"wrong_answers": 0, "errors": 0, "unsupported": 0},
        "known_inputs": len(next(iter(byconfig.values()))),
        "model_passes": dict(passes),
        "model_passes_total": sum(passes.values()),
        "hosts": sorted({m["host"] for m in metas}),
        "cpu_model": metas[0]["cpu_model"],
        "timing_finished": max(m["finished"] for m in metas),
        "models_started": min(m["started"] for m in model_metas),
        "raw_artifacts_verified": args.verify_artifacts,
        "per_configuration": {},
        "baseline_comparisons": {},
        "factor_contrasts": {},
    }
    for config, values in byconfig.items():
        report["per_configuration"][config] = summarize_group(list(values.values()))
        for category in ("family", "expected", "stratum"):
            grouped = defaultdict(list)
            for value in values.values():
                grouped[value[category]].append(value)
            report["per_configuration"][config]["by_" + category] = {
                group: summarize_group(v) for group, v in sorted(grouped.items())
            }
        report["baseline_comparisons"][config] = comparison(
            byconfig[plan["baseline"]], values, plan["seed"]
        )
    for rule in ("bland", "dantzig", "sparse", "soi"):
        for fixed in (0, 1):
            a, b = f"{rule}-g0-f{fixed}", f"{rule}-g1-f{fixed}"
            report["factor_contrasts"][f"gaussian-on:{rule}:f{fixed}"] = comparison(
                byconfig[a], byconfig[b], plan["seed"]
            )
        for gaussian in (0, 1):
            a, b = f"{rule}-g{gaussian}-f0", f"{rule}-g{gaussian}-f1"
            report["factor_contrasts"][f"fixed-on:{rule}:g{gaussian}"] = comparison(
                byconfig[a], byconfig[b], plan["seed"]
            )
    report["unknown_candidate"] = [
        v for v in compacted.values() if v["expected"] == "unknown"
    ]
    args.out.mkdir(parents=True, exist_ok=False)
    with (args.out / "per-input.jsonl").open("w") as stream:
        for key, value in sorted(compacted.items()):
            stream.write(json.dumps(value) + "\n")
    report["per_input_sha256"] = digest(args.out / "per-input.jsonl")
    write_json(args.out / "report.json", report)
    with (args.out / "summary.csv").open("w") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            [
                "configuration",
                "solved_all_3",
                "timeouts_rows",
                "mean_repeat_par2_seconds",
                "paired_process_speedup",
                "paired_check_speedup",
            ]
        )
        for cfg in plan["configurations"]:
            name = cfg["id"]
            summary = report["per_configuration"][name]
            cmp = report["baseline_comparisons"][name]
            writer.writerow(
                [
                    name,
                    summary["solved_all_repeats"],
                    summary["outcomes"].get("timeout", 0),
                    summary["mean_repeat_par2_seconds"],
                    cmp["process_seconds"]["geomean"],
                    cmp["check_ns"]["geomean"],
                ]
            )
    print(
        json.dumps(
            {
                "timing_rows": report["timing_rows"],
                "model_passes": report["model_passes_total"],
                "report": str(args.out / "report.json"),
            }
        )
    )


if __name__ == "__main__":
    main()
