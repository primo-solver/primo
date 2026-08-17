"""Compare two Primo binaries locally on answers, statistics, and wall time."""

import argparse
import json
import os
import pathlib
import statistics
import subprocess
import time

DECISIVE_ANSWERS = {"sat", "unsat", "unknown"}


def load_manifest(path: pathlib.Path) -> list[str]:
    return [
        line
        for raw in path.read_text().splitlines()
        if (line := raw.strip()) and not line.startswith("#")
    ]


def parse_statistics(stderr: str) -> tuple[dict[str, str], list[str]]:
    stats: dict[str, str] = {}
    diagnostics: list[str] = []
    for line in stderr.splitlines():
        if line.startswith(":") and " " in line:
            key, value = line.split(maxsplit=1)
            if not key.startswith(":time."):
                stats[key] = value
        elif line.strip():
            diagnostics.append(line)
    return stats, diagnostics


def run_solver(binary: pathlib.Path, benchmark: pathlib.Path, timeout: float) -> dict:
    started = time.monotonic()
    try:
        result = subprocess.run(
            [str(binary), "--quiet", "--stats", str(benchmark)],
            capture_output=True,
            check=False,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        return {
            "status": "timeout",
            "answer": "",
            "seconds": time.monotonic() - started,
            "returncode": None,
            "stats": {},
            "diagnostics": [str(error)],
        }

    answer = next(
        (
            line.strip()
            for line in result.stdout.splitlines()
            if line.strip() in DECISIVE_ANSWERS
        ),
        "",
    )
    stats, diagnostics = parse_statistics(result.stderr)
    status = "ok" if result.returncode == 0 and answer else "error"
    return {
        "status": status,
        "answer": answer,
        "seconds": time.monotonic() - started,
        "returncode": result.returncode,
        "stats": stats,
        "diagnostics": diagnostics,
    }


def summarize(rows: list[dict], files: list[str], repeats: int) -> dict:
    file_summaries = []
    for filename in files:
        by_solver = {
            solver: [
                row
                for row in rows
                if row["file"] == filename and row["solver"] == solver
            ]
            for solver in ("baseline", "candidate")
        }
        answers_match = (
            all(
                row["status"] == "ok"
                and row["answer"] == by_solver["baseline"][0]["answer"]
                for solver_rows in by_solver.values()
                for row in solver_rows
            )
            and by_solver["baseline"][0]["answer"]
            == by_solver["candidate"][0]["answer"]
        )
        reference_stats = by_solver["baseline"][0]["stats"]
        stats_match = all(
            row["stats"] == reference_stats
            for solver_rows in by_solver.values()
            for row in solver_rows
        )
        baseline_median = statistics.median(
            row["seconds"] for row in by_solver["baseline"]
        )
        candidate_median = statistics.median(
            row["seconds"] for row in by_solver["candidate"]
        )
        file_summaries.append(
            {
                "file": filename,
                "answer": by_solver["baseline"][0]["answer"]
                if answers_match
                else "disagreement",
                "answers_match": answers_match,
                "non_time_stats_match": stats_match,
                "baseline_median_seconds": baseline_median,
                "candidate_median_seconds": candidate_median,
                "candidate_over_baseline": candidate_median / baseline_median,
            }
        )

    return {
        "repeats": repeats,
        "files": file_summaries,
        "all_answers_match": all(item["answers_match"] for item in file_summaries),
        "all_non_time_stats_match": all(
            item["non_time_stats_match"] for item in file_summaries
        ),
        "baseline_median_sum_seconds": sum(
            item["baseline_median_seconds"] for item in file_summaries
        ),
        "candidate_median_sum_seconds": sum(
            item["candidate_median_seconds"] for item in file_summaries
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", required=True, type=pathlib.Path)
    parser.add_argument("--root", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--cpu", type=int)
    args = parser.parse_args()

    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})

    files = load_manifest(args.manifest)
    args.output.mkdir(parents=True, exist_ok=True)
    journal_path = args.output / "runs.jsonl"
    rows: list[dict] = []
    binaries = {
        "baseline": args.baseline.resolve(),
        "candidate": args.candidate.resolve(),
    }

    with journal_path.open("w") as journal:
        for repeat in range(args.repeats):
            for file_index, filename in enumerate(files):
                order = ["baseline", "candidate"]
                if (repeat + file_index) % 2:
                    order.reverse()
                for solver in order:
                    row = run_solver(
                        binaries[solver], args.root / filename, args.timeout
                    )
                    row.update({"repeat": repeat, "file": filename, "solver": solver})
                    rows.append(row)
                    journal.write(json.dumps(row, sort_keys=True) + "\n")
                    journal.flush()
                    os.fsync(journal.fileno())
                    print(
                        f"{solver:9s} {row['seconds']:8.3f}s {row['status']:7s} "
                        f"{row['answer']:7s} {filename}",
                        flush=True,
                    )

    summary = summarize(rows, files, args.repeats)
    summary.update(
        {
            "baseline": str(binaries["baseline"]),
            "candidate": str(binaries["candidate"]),
            "manifest": str(args.manifest.resolve()),
            "root": str(args.root.resolve()),
            "timeout_seconds": args.timeout,
            "cpu": args.cpu,
        }
    )
    summary_path = args.output / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return (
        0 if summary["all_answers_match"] and summary["all_non_time_stats_match"] else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
