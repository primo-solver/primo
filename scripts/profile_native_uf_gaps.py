#!/usr/bin/env python3
"""Rank qualified native UF timings and profile selected sessions with gprof.

Use the existing stdin runner/protocol classifier. Local instrumented runtimes
are diagnostics, never replacements for the original SLURM timing records.
"""

import argparse
import csv
import json
import os
import platform
import re
import statistics
import subprocess
import sys
from pathlib import Path

from native_uf_sessions import classify, command_spans, digest, run_session
from summarize_native_uf import load_rows


def read_stats(path):
    values = {}
    for key, value in re.findall(r"^(:[^ ]+) ([0-9]+)$", path.read_text(), re.MULTILINE):
        values.setdefault(key, []).append(int(value))
    return {key: {"sum": sum(items), "last": items[-1], "queries": len(items)}
            for key, items in values.items()}



def reference_problem(original):
    pieces = []
    cursor = 0
    for _, end, head in command_spans(original):
        if head == ("check-sat",):
            pieces.extend([original[cursor:end], "\n(get-info :all-statistics)\n"])
            cursor = end
    pieces.append(original[cursor:])
    return "".join(pieces)


def reference_result(stdout, code, timed_out, expected):
    protocol = "\n".join(line.strip() for line in stdout.splitlines()
                         if line.strip() in {"sat", "unsat", "unknown"})
    if "(error" in stdout:
        protocol += "\nreference-error"
    result = classify(protocol, code, timed_out, expected)
    result["reference_statistics"] = [dict(re.findall(
        r":([a-zA-Z0-9_-]+)\s+([0-9.]+)", block)) for block in re.split(
            r"(?m)^(?:sat|unsat|unknown)\s*$", stdout)[1:]]
    if (result["outcome"] in {"complete", "unknown"}
            and any(not block for block in result["reference_statistics"])):
        result["outcome"] = "error"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timing", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--expected-git", required=True)
    parser.add_argument("--reference-stats", type=Path,
                        help="collect pinned Z3 statistics after each query instead of gprof")
    parser.add_argument("--plain", action="store_true", help="native diagnostic without gprof")
    parser.add_argument("--omit-stats", action="store_true", help="omit CLI statistics in plain mode")
    parser.add_argument("--indices", type=int, nargs="+", required=True)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.plain and args.reference_stats:
        parser.error("--plain and --reference-stats are separate diagnostic modes")
    if args.omit_stats and (not args.plain or args.reference_stats):
        parser.error("--omit-stats requires native --plain mode")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = args.solver.resolve()
    root = args.root.resolve()
    timing = args.timing.resolve()
    binary_hash = digest(binary)
    version = subprocess.check_output([str(binary), "--version"], text=True, timeout=10)
    if f"git: {args.expected_git}" not in version.splitlines():
        raise ValueError("profiling binary revision mismatch")
    baseline_run = json.loads((timing / "assumptions" / "shard-0" / "run.json").read_text())
    if digest(args.manifest) != baseline_run["manifest_sha256"]:
        raise ValueError("manifest differs from the qualified native timing run")
    entries = json.loads(args.manifest.read_text())["entries"]
    data = {label: load_rows(timing / label) for label in ("assumptions", "rebuild", "z3")}
    native, reference = data["assumptions"], data["z3"]
    if any(row["outcome"] != "complete" for row in reference.values()):
        raise ValueError("profiling requires complete qualified Z3 reference sessions")
    if any(set(rows) != set(native) for rows in data.values()):
        raise ValueError("incomplete comparison coverage")
    for label, rows in data.items():
        with (out / f"{label}.csv").open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["file", "status", "answer", "seconds"])
            writer.writeheader()
            for index, row in sorted(rows.items()):
                expected = reference[index]["answers"]
                if row["outcome"] not in {"complete", "unknown", "timeout"}:
                    raise ValueError(f"untriaged timing outcome: {label}/{index}")
                if row["mismatches"] or any(a != b for a, b in zip(row["answers"], expected)
                                           if a in {"sat", "unsat"} and b in {"sat", "unsat"}):
                    raise ValueError(f"untriaged reference disagreement: {label}/{index}")
                # The legacy ranker has one answer column. Eligibility is based
                # on whole-session completion; all definite answers were checked above.
                writer.writerow({"file": row["file"], "status": "ok" if
                                 row["outcome"] == "complete" else row["outcome"],
                                 "answer": row["answers"][-1] if row["answers"] else "",
                                 "seconds": row["seconds"]})
    ranker = Path(__file__).resolve().with_name("rank_qfuf_gaps.py")
    subprocess.run([sys.executable, str(ranker), "--native", str(out / "assumptions.csv"),
                    "--ref", str(out / "z3.csv"), "--timeout", "30", "--fast-ref", "1",
                    "--top", "50", "--out", str(out / "ranked.csv")], check=True)
    comparison = {}
    for family in ("", "2018-Goel-hwbench", "20190906-CLEARSY"):
        ids = [i for i, row in native.items() if family in row["file"] and
               row["outcome"] == reference[i]["outcome"] == "complete"]
        ratios = sorted(native[i]["seconds"] / reference[i]["seconds"] for i in ids)
        comparison[family or "all"] = {
            "paired_complete": len(ids), "median_primo_over_z3": statistics.median(ratios),
            "p90_primo_over_z3": ratios[int(0.9 * len(ratios))],
            "at_least_5x": sum(r >= 5 for r in ratios),
            "at_least_10x": sum(r >= 10 for r in ratios),
            "primo_seconds": sum(native[i]["seconds"] for i in ids),
            "z3_seconds": sum(reference[i]["seconds"] for i in ids)}
    command = [str(binary), "--quiet", "--stats", "--incremental-mode=assumptions",
               "--max-theory-conflicts=10000"]
    if args.omit_stats:
        command.remove("--stats")
    reference_metadata = None
    if args.reference_stats:
        binary = args.reference_stats.resolve()
        recorded = json.loads((timing / "z3" / "shard-0" / "run.json").read_text())
        binary_hash = digest(binary)
        if binary_hash != recorded["solver_sha256"]:
            raise ValueError("reference differs from qualified Z3 binary")
        version = subprocess.check_output([str(binary), "--version"], text=True, timeout=10)
        command = [str(binary), "-smt2", "-in"]
        reference_metadata = {"sha256": binary_hash, "version": version}
    metadata = {"command": command, "timeout": args.timeout, "version": version,
                "binary_sha256": binary_hash, "driver_sha256": digest(Path(__file__)),
                "manifest_sha256": digest(args.manifest), "host": platform.node(),
                "platform": platform.platform(), "timing_root": str(timing),
                "gprof": (None if args.plain or args.reference_stats else
                          subprocess.check_output(["gprof", "--version"],
                                                  text=True).splitlines()[0]),
                "comparison": comparison, "profiles": [],
                "reference_stats": reference_metadata, "plain": args.plain,
                "limitations": ("Read-only get-info :all-statistics inserted after each query; "
                                "local diagnostic times do not replace qualified reference timings."
                                if args.reference_stats else
                                "Local uninstrumented diagnostic; not a headline timing comparison."
                                if args.plain else
                                "Local -pg diagnostics; instrumented owned code and an "
                                "uninstrumented SAT library. Nested phase times overlap; "
                                "only query-local counters should be summed.")}
    for index in args.indices:
        row = native[index]
        source = root / row["file"]
        if digest(source) != entries[index]["sha256"]:
            raise ValueError("input checksum mismatch")
        case = out / f"session-{index:04d}"
        case.mkdir()
        run_source = source
        if args.reference_stats:
            run_source = case / "reference-with-stats.smt2"
            run_source.write_text(reference_problem(source.read_text()))
        cwd = Path.cwd()
        try:
            os.chdir(case)
            code, timed_out, seconds = run_session(command, run_source, case, args.timeout)
        finally:
            os.chdir(cwd)
        stdout = (case / "stdout").read_text()
        if args.reference_stats:
            result = reference_result(stdout, code, timed_out, reference[index]["answers"])
            result["diagnostic_input_sha256"] = digest(run_source)
        else:
            result = classify(stdout, code, timed_out, reference[index]["answers"])
        result.update(index=index, file=row["file"], seconds=seconds,
                      input_sha256=digest(source), stats=read_stats(case / "stderr"),
                      qualified_timing={label: data[label][index] for label in data},
                      qualified_stats=read_stats(timing / "assumptions" /
                                                f"shard-{index % 16}" /
                                                f"session-{index:04d}" / "stderr"))
        if (not args.reference_stats and not args.plain
                and result["outcome"] in {"complete", "unknown"}):
            if not (case / "gmon.out").exists():
                raise ValueError("gmon.out missing; use a -pg binary or explicit --plain")
            with (case / "gprof.txt").open("w") as stream:
                subprocess.run(["gprof", "-b", str(binary), str(case / "gmon.out")],
                               stdout=stream, check=True)
        (case / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        metadata["profiles"].append(result)
        (out / "summary.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print(json.dumps({"index": index, "outcome": result["outcome"], "seconds": seconds}),
              flush=True)
        if (result["outcome"] in {"wrong-answer", "error", "timeout"}
                or (args.reference_stats and result["outcome"] != "complete")):
            return 1
    if digest(binary) != binary_hash:
        raise ValueError("profiling binary changed during run")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
