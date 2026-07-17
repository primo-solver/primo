#!/usr/bin/env python3
"""Benchmark Primo on the current interesting QF_UF families.

The default file discovery uses local benchmark material under tmp/QF_UF.  The
script records the status columns needed for PAR2 tracking without depending on
non-standard Python packages.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import html.parser
import subprocess
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


DEFAULT_FAMILY_GLOBS = {
    "Goel-hwbench": "tmp/QF_UF/2018-Goel-hwbench/*.smt2",
    "NEQ": "tmp/QF_UF/**/NEQ*.smt2",
    "PEQ": "tmp/QF_UF/**/PEQ*.smt2",
    "QG-classification": "tmp/QF_UF/**/QG*.smt2",
    "SEQ": "tmp/QF_UF/**/SEQ*.smt2",
}

ANSWERS = {"sat", "unsat", "unknown", "unsupported"}
CHECKABLE_ANSWERS = {"sat", "unsat"}
DEFAULT_MANIFEST = Path("benchmarks/interesting-qf-uf.txt")


class TableTextExtractor(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.tables: list[list[list[str]]] = []
        self._current_table: list[list[str]] | None = None
        self._current_row: list[str] | None = None
        self._current_cell: list[str] | None = None

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        del attrs
        if tag == "table":
            self._current_table = []
        elif tag == "tr" and self._current_table is not None:
            self._current_row = []
        elif tag in {"td", "th"} and self._current_row is not None:
            self._current_cell = []

    def handle_data(self, data: str) -> None:
        if self._current_cell is not None:
            self._current_cell.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag in {"td", "th"} and self._current_cell is not None and self._current_row is not None:
            self._current_row.append(" ".join("".join(self._current_cell).split()))
            self._current_cell = None
        elif tag == "tr" and self._current_row is not None and self._current_table is not None:
            if self._current_row:
                self._current_table.append(self._current_row)
            self._current_row = None
        elif tag == "table" and self._current_table is not None:
            if self._current_table:
                self.tables.append(self._current_table)
            self._current_table = None


def extract_reference_rows(html_path: Path) -> list[dict[str, str]]:
    extractor = TableTextExtractor()
    extractor.feed(html_path.read_text(errors="replace"))
    rows: list[dict[str, str]] = []
    for table in extractor.tables:
        if len(table) < 2:
            continue
        header = table[0]
        lowered = [cell.lower() for cell in header]
        if not any("llm2smt" in cell or cell in {"z3", "yices", "opensmt"} for cell in lowered):
            continue
        if not any("file" in cell or "benchmark" in cell or "instance" in cell for cell in lowered):
            continue
        for raw_row in table[1:]:
            row = {header[i]: raw_row[i] if i < len(raw_row) else "" for i in range(len(header))}
            rows.append(row)
    return rows


def discover_files(repo: Path) -> list[tuple[str, Path]]:
    discovered: list[tuple[str, Path]] = []
    seen: set[Path] = set()
    for family, pattern in DEFAULT_FAMILY_GLOBS.items():
        for path in sorted(repo.glob(pattern)):
            if path in seen or not path.is_file():
                continue
            seen.add(path)
            discovered.append((family, path))
    return discovered


def discover_manifest_files(repo: Path, manifest: Path) -> list[tuple[str, Path]]:
    discovered: list[tuple[str, Path]] = []
    for line_number, raw_line in enumerate(manifest.read_text().splitlines(), start=1):
        entry = raw_line.strip()
        if not entry or entry.startswith("#"):
            continue
        relative = Path(entry if entry.endswith(".smt2") else f"{entry}.smt2")
        path = repo / "tmp" / "QF_UF" / relative
        if not path.is_file():
            raise SystemExit(f"{manifest}:{line_number}: missing benchmark {path}")
        family = entry.split("/", 1)[0]
        if family == "2018-Goel-hwbench":
            family = "Goel"
        elif family == "QG-classification":
            family = "QG"
        discovered.append((family, path))
    return discovered


def completed_output_text(output: str | bytes | None) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode(errors="replace")
    return output


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
        stdout = completed_output_text(exc.stdout)
        stderr = completed_output_text(exc.stderr)
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
        "seconds": seconds,
        "timeout": timed_out,
        "exit_code": exit_code,
        "stdout": stdout,
        "stderr": stderr,
    }


def safe_log_stem(index: int, path: Path) -> str:
    digest = hashlib.sha256(str(path).encode()).hexdigest()[:12]
    return f"{index:04d}-{digest}-{path.name}"


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


def parse_smtlib_status(path: Path) -> str:
    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.strip()
        if not line.startswith("(set-info :status "):
            continue
        status = line.removeprefix("(set-info :status ").rstrip(")").strip()
        return status if status in CHECKABLE_ANSWERS else ""
    return ""


def load_expected(path: Path | None) -> dict[str, str]:
    if path is None:
        return {}
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        if "file" not in (reader.fieldnames or []) or "answer" not in (reader.fieldnames or []):
            raise SystemExit("--expected CSV must contain file and answer columns")
        return {row["file"]: row["answer"] for row in reader if row.get("file")}


def stat_value(row: dict[str, object], key: str) -> int:
    try:
        return int(str(row.get(key, "") or "0"))
    except ValueError:
        return 0


def evaluate_file(
    index: int,
    total: int,
    family: str,
    path: Path,
    repo: Path,
    solver_command: list[str],
    timeout: float,
    expected: dict[str, str],
    expected_from_status: bool,
    log_dir: Path | None,
) -> dict[str, object]:
    result = run_solver(solver_command, path, timeout)
    stdout_log = ""
    stderr_log = ""
    if log_dir is not None:
        log_dir.mkdir(parents=True, exist_ok=True)
        stem = safe_log_stem(index, path)
        stdout_path = log_dir / f"{stem}.stdout"
        stderr_path = log_dir / f"{stem}.stderr"
        stdout_path.write_text(str(result["stdout"]))
        stderr_path.write_text(str(result["stderr"]))
        stdout_log = str(stdout_path)
        stderr_log = str(stderr_path)
    expected_answer = expected.get(str(path), expected.get(str(path.relative_to(repo)), ""))
    if not expected_answer and expected_from_status:
        expected_answer = parse_smtlib_status(path)
    wrong = (
        bool(expected_answer)
        and result["status"] == "ok"
        and result["answer"] in {"sat", "unsat"}
        and result["answer"] != expected_answer
    )
    stats = parse_stats(str(result["stderr"]))
    row = {
        "row": index,
        "file": str(path),
        "relative_file": str(path.relative_to(repo)),
        "family": family,
        "solver_args": " ".join(solver_command),
        "status": result["status"],
        "answer": result["answer"],
        "expected": expected_answer,
        "wrong": str(wrong).lower(),
        "seconds": f"{float(result['seconds']):.6f}",
        "timeout": str(result["timeout"]).lower(),
        "timed_out_with_stats": str(result["status"] == "timeout" and bool(stats)).lower(),
        "exit_code": result["exit_code"],
        "stdout_log": stdout_log,
        "stderr_log": stderr_log,
        "_index": index,
        "_total": total,
    }
    row.update(stats)
    print(
        f"[{index}/{total}] {family} {path}: "
        f"{row['status']} {row['answer']} {float(result['seconds']):.3f}s",
        flush=True,
    )
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", nargs="+", default=["./build/primo", "--quiet"])
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--out", type=Path, default=Path("results/interesting-qf-uf.csv"))
    parser.add_argument(
        "--summary", type=Path, default=Path("results/interesting-qf-uf-summary.csv")
    )
    parser.add_argument("--expected", type=Path)
    parser.add_argument(
        "--no-expected-from-status",
        action="store_true",
        help="Do not use SMT-LIB :status metadata as expected sat/unsat answers",
    )
    parser.add_argument(
        "--reference-html",
        type=Path,
        help="Optional interesting.html file; extracted tables are written to --reference-out",
    )
    parser.add_argument(
        "--reference-out",
        type=Path,
        default=Path("results/interesting-qf-uf-reference.csv"),
    )
    parser.add_argument("--limit", type=int, help="Run only the first N discovered files")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="Run exact tmp/QF_UF-relative paths; defaults to benchmarks/interesting-qf-uf.txt",
    )
    parser.add_argument(
        "--discover",
        action="store_true",
        help="Ignore --manifest and use broad local family discovery",
    )
    parser.add_argument("--jobs", type=int, default=1, help="Number of solver processes to run")
    parser.add_argument("--shards", type=int, default=1, help="Split files into this many shards")
    parser.add_argument("--shard-index", type=int, default=0, help="Zero-based shard index to run")
    parser.add_argument("--log-dir", type=Path, help="Write per-instance stdout/stderr logs")
    parser.add_argument("--repo", type=Path, default=Path("."))
    args = parser.parse_args()

    repo = args.repo.resolve()
    if args.reference_html is not None:
        reference_rows = extract_reference_rows(args.reference_html)
        args.reference_out.parent.mkdir(parents=True, exist_ok=True)
        fieldnames = sorted({key for row in reference_rows for key in row})
        with args.reference_out.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(reference_rows)
        print(f"wrote {args.reference_out}")

    files = discover_files(repo) if args.discover else discover_manifest_files(repo, args.manifest)
    if not files:
        raise SystemExit("no interesting QF_UF files found under tmp/QF_UF")
    if args.limit is not None:
        if args.limit < 0:
            raise SystemExit("--limit must be nonnegative")
        files = files[: args.limit]
    if args.jobs < 1:
        raise SystemExit("--jobs must be positive")
    if args.shards < 1:
        raise SystemExit("--shards must be positive")
    if args.shard_index < 0 or args.shard_index >= args.shards:
        raise SystemExit("--shard-index must be in [0, shards)")
    if args.shards > 1:
        files = [item for offset, item in enumerate(files) if offset % args.shards == args.shard_index]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    expected = load_expected(args.expected)
    expected_from_status = not args.no_expected_from_status
    rows: list[dict[str, object]] = []
    solver_command = [*args.solver, *args.solver_arg]
    total_files = len(files)
    if args.jobs == 1:
        for index, (family, path) in enumerate(files, start=1):
            rows.append(
                evaluate_file(
                    index,
                    total_files,
                    family,
                    path,
                    repo,
                    solver_command,
                    args.timeout,
                    expected,
                    expected_from_status,
                    args.log_dir,
                )
            )
    else:
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = [
                executor.submit(
                    evaluate_file,
                    index,
                    total_files,
                    family,
                    path,
                    repo,
                    solver_command,
                    args.timeout,
                    expected,
                    expected_from_status,
                    args.log_dir,
                )
                for index, (family, path) in enumerate(files, start=1)
            ]
            for future in as_completed(futures):
                rows.append(future.result())
        rows.sort(key=lambda row: int(row["_index"]))
    for row in rows:
        row.pop("_index", None)
        row.pop("_total", None)

    fieldnames = [
        "row",
        "file",
        "relative_file",
        "family",
        "solver_args",
        "status",
        "answer",
        "expected",
        "wrong",
        "seconds",
        "timeout",
        "timed_out_with_stats",
        "exit_code",
        "stdout_log",
        "stderr_log",
    ]
    stat_fields = sorted({key for row in rows for key in row if key.startswith(":")})
    fieldnames.extend(stat_fields)
    with args.out.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    by_family_ok = Counter(row["family"] for row in rows if row["status"] == "ok")
    by_family_solved = Counter(
        row["family"] for row in rows if row["status"] == "ok" and row["answer"] in {"sat", "unsat"}
    )
    by_status = Counter(str(row["status"]) for row in rows)
    by_answer = Counter(str(row["answer"]) for row in rows)
    all_families = sorted({row["family"] for row in rows})
    stat_totals = {
        key: sum(stat_value(row, key) for row in rows)
        for key in stat_fields
    }
    total = len(rows)
    solved = sum(1 for row in rows if row["status"] == "ok" and row["answer"] in {"sat", "unsat"})
    checked = sum(1 for row in rows if row["expected"])
    wrong = sum(1 for row in rows if row["wrong"] == "true")
    timed_out_with_stats = sum(1 for row in rows if row["timed_out_with_stats"] == "true")
    par2 = sum(
        float(row["seconds"]) if row["status"] == "ok" else 2.0 * args.timeout for row in rows
    )
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    with args.summary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["metric", "value"])
        writer.writeheader()
        writer.writerow({"metric": "total", "value": total})
        writer.writerow({"metric": "solved", "value": solved})
        writer.writerow({"metric": "checked", "value": checked})
        writer.writerow({"metric": "wrong", "value": wrong})
        writer.writerow({"metric": "timed_out_with_stats", "value": timed_out_with_stats})
        writer.writerow({"metric": "par2", "value": f"{par2:.6f}"})
        for status, count in sorted(by_status.items()):
            writer.writerow({"metric": f"status.{status}", "value": count})
        for answer, count in sorted(by_answer.items()):
            writer.writerow({"metric": f"answer.{answer}", "value": count})
        for family in all_families:
            writer.writerow({"metric": f"family.{family}.ok", "value": by_family_ok[family]})
            writer.writerow(
                {"metric": f"family.{family}.solved", "value": by_family_solved[family]}
            )
        for stat, total_value in sorted(stat_totals.items()):
            writer.writerow({"metric": f"stat-total.{stat}", "value": total_value})
    print(f"wrote {args.out}")
    print(f"wrote {args.summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
