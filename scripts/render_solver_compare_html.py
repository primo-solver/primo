#!/usr/bin/env python3
"""Render a per-instance x per-solver colored HTML comparison table from
bench_qf_uf_full.py's per-solver CSVs (e.g. results/qf-lra-solver-compare-<job>/).

Styled after https://people.ciirc.cvut.cz/~janotmik/llm2smt/primo.html
(a StarExec-style results grid: one row per benchmark, one column per
solver, cell background color-coded by outcome) but without the embedded
SMT-LIB source snippets -- just the instance name, per-solver time, and
color coding.

Usage:
    python3 scripts/render_solver_compare_html.py RESULTS_DIR [--out FILE]
        [--solvers NAME [NAME ...]] [--title TITLE]

RESULTS_DIR must contain one CSV per solver (e.g. primo.csv, z3.csv,
cvc5.csv) in bench_qf_uf_full.py's per-file row format (columns: row,
file, family, status, answer, expected, wrong, seconds, timeout, ...).
Solver order defaults to alphabetical with "primo" first if present;
override with --solvers to control the exact column order.
"""

from __future__ import annotations

import argparse
import csv
import html
from collections import Counter, defaultdict
from pathlib import Path

CONCRETE_ANSWERS = {"sat", "unsat"}

COLOR_SOLVED = "#00a86b"
COLOR_WIN = "#00c010"
COLOR_UNIQUE = "#00F000"
COLOR_WRONG = "#F00000"
COLOR_TIMEOUT = "#FA8072"
COLOR_UNSUPPORTED = "#4682B4"
COLOR_UNKNOWN = "black"
COLOR_ROW_LABEL = "#F1F1F1"

LEGEND = [
    (COLOR_TIMEOUT, "black", "TIMEOUT / ERROR"),
    (COLOR_WRONG, "white", "WRONG"),
    (COLOR_SOLVED, "white", "SOLVED"),
    (COLOR_WIN, "white", "FASTEST (WIN)"),
    (COLOR_UNIQUE, "black", "UNIQUE (only solver to solve it)"),
    (COLOR_UNSUPPORTED, "white", "UNSUPPORTED INPUT"),
    (COLOR_UNKNOWN, "white", "NOT RUN"),
]


def load_csv(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="") as handle:
        return {row["file"]: row for row in csv.DictReader(handle)}


def discover_solvers(results_dir: Path, explicit: list[str] | None) -> list[str]:
    if explicit:
        return explicit
    names = sorted(
        p.stem for p in results_dir.glob("*.csv") if not p.stem.endswith("-summary")
    )
    if "primo" in names:
        names.remove("primo")
        names.insert(0, "primo")
    return names


def classify_row(cells: dict[str, dict[str, str]]) -> dict[str, tuple[str, str, str]]:
    """Returns, per solver: (bgcolor, fg_color, label_text)."""
    concrete: dict[str, str] = {}
    for name, row in cells.items():
        if row is not None and row["status"] == "ok" and row["answer"] in CONCRETE_ANSWERS:
            concrete[name] = row["answer"]

    majority = None
    if concrete:
        counts = Counter(concrete.values())
        majority = counts.most_common(1)[0][0]

    solved_names = []
    for name, row in cells.items():
        if row is None or name not in concrete:
            continue
        embedded_wrong = row.get("wrong", "false") == "true"
        cross_solver_wrong = majority is not None and concrete[name] != majority
        if not (embedded_wrong or cross_solver_wrong):
            solved_names.append(name)

    best_time = None
    if solved_names:
        best_time = min(float(cells[name]["seconds"]) for name in solved_names)

    result: dict[str, tuple[str, str, str]] = {}
    for name, row in cells.items():
        if row is None:
            result[name] = (COLOR_UNKNOWN, "white", "not run")
            continue
        seconds = float(row["seconds"])
        time_text = f"{seconds:.2f}s"
        embedded_wrong = row.get("wrong", "false") == "true"
        cross_solver_wrong = (
            name in concrete and majority is not None and concrete[name] != majority
        )
        if embedded_wrong or cross_solver_wrong:
            result[name] = (COLOR_WRONG, "white", f"{row['answer']} {time_text} (wrong)")
        elif name in solved_names:
            if len(solved_names) == 1:
                result[name] = (COLOR_UNIQUE, "black", f"{row['answer']} {time_text}")
            elif seconds == best_time:
                result[name] = (COLOR_WIN, "white", f"{row['answer']} {time_text}")
            else:
                result[name] = (COLOR_SOLVED, "white", f"{row['answer']} {time_text}")
        elif row["answer"] == "unsupported":
            result[name] = (COLOR_UNSUPPORTED, "white", f"unsupported {time_text}")
        else:
            status_text = row["status"] if row["status"] != "ok" else row["answer"]
            result[name] = (COLOR_TIMEOUT, "black", f"{status_text} {time_text}")
    return result


def render_html(results_dir: Path, solver_names: list[str], title: str) -> str:
    data = {name: load_csv(results_dir / f"{name}.csv") for name in solver_names}
    all_files = sorted(set().union(*(d.keys() for d in data.values())))
    family_of: dict[str, str] = {}
    for name in solver_names:
        for file_path, row in data[name].items():
            family_of.setdefault(file_path, row.get("family", ""))
    all_files.sort(key=lambda f: (family_of.get(f, ""), f))

    solved_counts = {name: 0 for name in solver_names}
    win_counts = {name: 0 for name in solver_names}
    total = len(all_files)

    rows_html = []
    for file_path in all_files:
        cells = {name: data[name].get(file_path) for name in solver_names}
        classified = classify_row(cells)
        cell_html = []
        for name in solver_names:
            bg, fg, label = classified[name]
            if bg in (COLOR_SOLVED, COLOR_WIN, COLOR_UNIQUE):
                solved_counts[name] += 1
            if bg == COLOR_WIN:
                win_counts[name] += 1
            cell_html.append(
                f'<td bgcolor="{bg}"><font color="{fg}">{html.escape(label)}</font></td>'
            )
        rows_html.append(
            f'<tr><td bgcolor="{COLOR_ROW_LABEL}">{html.escape(file_path)}</td>'
            + "".join(cell_html)
            + "</tr>"
        )

    legend_cells = "".join(
        f'<td bgcolor="{bg}"><font color="{fg}">{html.escape(text)}</font></td>'
        for bg, fg, text in LEGEND
    )
    header_cells = "".join(f"<th>{html.escape(name)}</th>" for name in solver_names)
    summary_cells = "".join(
        f"<td>{solved_counts[name]}/{total} solved ({win_counts[name]} fastest)</td>"
        for name in solver_names
    )

    return f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>{html.escape(title)}</title>
<style>
body {{ font-family: sans-serif; font-size: 13px; }}
table {{ border-collapse: collapse; }}
td, th {{ border: 1px solid #888; padding: 2px 6px; white-space: nowrap; }}
</style>
</head>
<body>
<h2>{html.escape(title)}</h2>
<table border="1">
<tr>{legend_cells}</tr>
</table>
<table border="1">
<tr><td></td>{summary_cells}</tr>
</table>
<table border="1">
<thead>
<tr><th>Problem</th>{header_cells}</tr>
</thead>
<tbody>
{"".join(rows_html)}
</tbody>
</table>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results_dir", type=Path)
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--solvers", nargs="+", default=None)
    parser.add_argument("--title", default=None)
    args = parser.parse_args()

    solver_names = discover_solvers(args.results_dir, args.solvers)
    if not solver_names:
        raise SystemExit(f"no solver CSVs found in {args.results_dir}")

    title = args.title or f"Solver comparison: {args.results_dir.name}"
    out_path = args.out or args.results_dir / "compare.html"
    out_path.write_text(render_html(args.results_dir, solver_names, title))
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
