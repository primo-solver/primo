#!/usr/bin/env python3
"""Run a sharded full QF_UF benchmark over ``tmp/QF_UF``.

The script is intentionally standard-library only so the same command works on
the workstation and on SLURM compute nodes.  It records enough information to
merge shards, compute PAR2, check wrong answers from SMT-LIB ``:status``
metadata, and summarize per-family progress.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import subprocess
import time
from collections import Counter
from pathlib import Path


ANSWERS = {"sat", "unsat", "unknown", "unsupported"}
CHECKABLE_ANSWERS = {"sat", "unsat"}


def family_for(path: Path, root: Path) -> str:
    relative = path.relative_to(root)
    first = relative.parts[0]
    if first == "2018-Goel-hwbench":
        return "Goel"
    if first == "QG-classification":
        return "QG"
    return first


def discover_files(root: Path) -> list[Path]:
    return sorted(path for path in root.rglob("*.smt2") if path.is_file())


def parse_smtlib_status(path: Path) -> str:
    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.strip()
        if not line.startswith("(set-info :status "):
            continue
        status = line.removeprefix("(set-info :status ").rstrip(")").strip()
        return status if status in CHECKABLE_ANSWERS else ""
    return ""


def run_solver(command: list[str], path: Path, timeout: float) -> dict[str, object]:
    start = time.perf_counter()
    timed_out = False
    try:
        proc = subprocess.run(
            [*command, str(path)],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
        stdout = proc.stdout
        stderr = proc.stderr
        exit_code = proc.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        exit_code = 124
    seconds = time.perf_counter() - start
    first_line = stdout.strip().splitlines()[0] if stdout.strip() else ""
    answer = first_line if first_line in ANSWERS else ""
    if timed_out:
        status = "timeout"
    elif exit_code != 0 and not answer:
        status = "error"
    else:
        status = "ok"
    return {
        "status": status,
        "answer": answer or "error",
        "seconds": f"{seconds:.6f}",
        "timeout": str(timed_out).lower(),
        "exit_code": exit_code,
        "stdout": stdout,
        "stderr": stderr,
        "stats": parse_stats(stderr),
        "stderr_head": " ".join(stderr.splitlines()[:2])[:240],
    }


def parse_stats(stderr: str) -> dict[str, str]:
    stats: dict[str, str] = {}
    for line in stderr.splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        parts = line.split(maxsplit=1)
        if len(parts) == 2:
            stats[parts[0]] = parts[1]
    return stats


def stat_value(row: dict[str, object], key: str) -> int:
    try:
        return int(str(row.get(key, "") or "0"))
    except ValueError:
        return 0


def safe_log_stem(index: int, path: Path) -> str:
    digest = hashlib.sha256(str(path).encode()).hexdigest()[:12]
    return f"{index:04d}-{digest}-{path.name}"


def write_summary(rows: list[dict[str, object]], summary: Path, timeout: float) -> None:
    total = len(rows)
    solved = sum(1 for row in rows if row["status"] == "ok" and row["answer"] in {"sat", "unsat"})
    unsupported = sum(1 for row in rows if row["answer"] == "unsupported")
    checked = sum(1 for row in rows if row["expected"])
    wrong = sum(1 for row in rows if row["wrong"] == "true")
    par2 = sum(
        float(row["seconds"])
        if row["status"] == "ok" and row["answer"] in {"sat", "unsat"}
        else 2.0 * timeout
        for row in rows
    )
    by_status = Counter(str(row["status"]) for row in rows)
    by_answer = Counter(str(row["answer"]) for row in rows)
    by_family_total = Counter(str(row["family"]) for row in rows)
    by_family_solved = Counter(
        str(row["family"])
        for row in rows
        if row["status"] == "ok" and row["answer"] in {"sat", "unsat"}
    )
    by_family_wrong = Counter(str(row["family"]) for row in rows if row["wrong"] == "true")
    by_family_timeout = Counter(str(row["family"]) for row in rows if row["status"] == "timeout")
    by_family_unsupported = Counter(
        str(row["family"]) for row in rows if row["answer"] == "unsupported"
    )
    stat_keys = sorted({key for row in rows for key in row if key.startswith(":")})

    summary.parent.mkdir(parents=True, exist_ok=True)
    with summary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["metric", "value"])
        writer.writeheader()
        writer.writerow({"metric": "total", "value": total})
        writer.writerow({"metric": "solved", "value": solved})
        writer.writerow({"metric": "unsupported", "value": unsupported})
        writer.writerow({"metric": "checked", "value": checked})
        writer.writerow({"metric": "wrong", "value": wrong})
        writer.writerow({"metric": "par2", "value": f"{par2:.6f}"})
        for status, count in sorted(by_status.items()):
            writer.writerow({"metric": f"status.{status}", "value": count})
        for answer, count in sorted(by_answer.items()):
            writer.writerow({"metric": f"answer.{answer}", "value": count})
        for family in sorted(by_family_total):
            writer.writerow({"metric": f"family.{family}.total", "value": by_family_total[family]})
            writer.writerow(
                {"metric": f"family.{family}.solved", "value": by_family_solved[family]}
            )
            writer.writerow(
                {"metric": f"family.{family}.timeout", "value": by_family_timeout[family]}
            )
            writer.writerow(
                {
                    "metric": f"family.{family}.unsupported",
                    "value": by_family_unsupported[family],
                }
            )
            writer.writerow({"metric": f"family.{family}.wrong", "value": by_family_wrong[family]})
        for key in stat_keys:
            total_value = sum(stat_value(row, key) for row in rows)
            active_rows = sum(1 for row in rows if stat_value(row, key) > 0)
            writer.writerow({"metric": f"stat-total.{key}", "value": total_value})
            writer.writerow({"metric": f"stat-active-files.{key}", "value": active_rows})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("tmp/QF_UF"))
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--solver", nargs="+", default=["./build/primo", "--quiet"])
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--out", type=Path, default=Path("results/qf-uf-full.csv"))
    parser.add_argument("--summary", type=Path, default=Path("results/qf-uf-full-summary.csv"))
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args()

    repo = args.repo.resolve()
    root = (repo / args.root).resolve()
    if not root.is_dir():
        raise SystemExit(f"missing QF_UF root: {root}")
    if args.shards < 1:
        raise SystemExit("--shards must be positive")
    if args.shard_index < 0 or args.shard_index >= args.shards:
        raise SystemExit("--shard-index must be in [0, shards)")

    all_files = discover_files(root)
    files = [path for offset, path in enumerate(all_files) if offset % args.shards == args.shard_index]
    solver_command = [*args.solver, *args.solver_arg]

    rows: list[dict[str, object]] = []
    for index, path in enumerate(files, start=1):
        result = run_solver(solver_command, path, args.timeout)
        expected = parse_smtlib_status(path)
        answer = str(result["answer"])
        wrong = (
            bool(expected)
            and result["status"] == "ok"
            and answer in CHECKABLE_ANSWERS
            and answer != expected
        )
        stdout_log = ""
        stderr_log = ""
        if args.log_dir is not None and result["status"] != "ok":
            args.log_dir.mkdir(parents=True, exist_ok=True)
            stem = safe_log_stem(index, path)
            stdout_log = str(args.log_dir / f"{stem}.stdout")
            stderr_log = str(args.log_dir / f"{stem}.stderr")
            Path(stdout_log).write_text(str(result["stdout"]))
            Path(stderr_log).write_text(str(result["stderr"]))
        row = {
            "row": index,
            "file": str(path.relative_to(repo)),
            "family": family_for(path, root),
            "status": result["status"],
            "answer": answer,
            "expected": expected,
            "wrong": str(wrong).lower(),
            "seconds": result["seconds"],
            "timeout": result["timeout"],
            "exit_code": result["exit_code"],
            "stderr_head": result["stderr_head"],
            "stdout_log": stdout_log,
            "stderr_log": stderr_log,
        }
        row.update(result["stats"])
        rows.append(row)
        print(
            f"[{index}/{len(files)}] {row['family']} {row['file']}: "
            f"{row['status']} {row['answer']} {row['seconds']}s",
            flush=True,
        )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    base_fieldnames = [
        "row",
        "file",
        "family",
        "status",
        "answer",
        "expected",
        "wrong",
        "seconds",
        "timeout",
        "exit_code",
        "stderr_head",
        "stdout_log",
        "stderr_log",
    ]
    stat_fieldnames = sorted({key for row in rows for key in row if key.startswith(":")})
    fieldnames = [*base_fieldnames, *stat_fieldnames]
    with args.out.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    write_summary(rows, args.summary, args.timeout)
    print(f"wrote {args.out}")
    print(f"wrote {args.summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
