#!/usr/bin/env python3
"""Render a per-instance x per-solver colored HTML comparison table from
bench_qf_uf_full.py's per-solver CSVs (e.g. results/qf-lra-solver-compare-<job>/).

Inspired by https://people.ciirc.cvut.cz/~janotmik/llm2smt/primo.html (a
StarExec-style results grid: one row per benchmark, one column per
solver, cell color-coded by outcome) but without the embedded SMT-LIB
source snippets -- just the instance name, per-solver time, and color
coding, plus a summary bar and a family-grouped, searchable table.

Usage:
    python3 scripts/render_solver_compare_html.py RESULTS_DIR [--out FILE]
        [--solvers NAME [NAME ...]] [--title TITLE] [--timeout SECONDS]

RESULTS_DIR must contain one CSV per solver (e.g. primo.csv, z3.csv,
cvc5.csv) in bench_qf_uf_full.py's per-file row format (columns: row,
file, family, status, answer, expected, wrong, seconds, timeout, ...).
Solver order defaults to alphabetical with "primo" first if present;
override with --solvers to control the exact column order. The output
is a self-contained HTML fragment (no <!doctype>/<html>/<head>/<body>
wrapper) -- valid to open directly via file:// (browsers tolerate the
missing shell) and directly usable as an Artifact publish source.
"""

from __future__ import annotations

import argparse
import csv
import html
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

CONCRETE_ANSWERS = {"sat", "unsat"}

# status key -> display label for the legend / cell text
STATUS_LABELS = {
    "win": "Fastest",
    "solved": "Solved",
    "unique": "Unique solve",
    "wrong": "Wrong answer",
    "timeout": "Timeout",
    "error": "Error",
    "unsupported": "Unsupported",
    "notrun": "Not run",
}
LEGEND_ORDER = ["win", "solved", "unique", "wrong", "timeout", "error", "unsupported", "notrun"]


@dataclass
class Cell:
    status: str  # key into STATUS_LABELS
    label: str  # short text shown in the cell (answer/status word)
    seconds: float | None


def load_csv(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="") as handle:
        return {row["file"]: row for row in csv.DictReader(handle)}


def discover_solvers(results_dir: Path, explicit: list[str] | None) -> list[str]:
    if explicit:
        return explicit
    names = sorted(p.stem for p in results_dir.glob("*.csv") if not p.stem.endswith("-summary"))
    if "primo" in names:
        names.remove("primo")
        names.insert(0, "primo")
    return names


def classify_row(cells: dict[str, dict[str, str] | None]) -> dict[str, Cell]:
    concrete: dict[str, str] = {}
    for name, row in cells.items():
        if row is not None and row["status"] == "ok" and row["answer"] in CONCRETE_ANSWERS:
            concrete[name] = row["answer"]

    majority = Counter(concrete.values()).most_common(1)[0][0] if concrete else None

    solved_names = [
        name
        for name, answer in concrete.items()
        if cells[name].get("wrong", "false") != "true" and (majority is None or answer == majority)
    ]
    best_time = min((float(cells[n]["seconds"]) for n in solved_names), default=None)

    result: dict[str, Cell] = {}
    for name, row in cells.items():
        if row is None:
            result[name] = Cell("notrun", "not run", None)
            continue
        seconds = float(row["seconds"])
        embedded_wrong = row.get("wrong", "false") == "true"
        cross_solver_wrong = name in concrete and majority is not None and concrete[name] != majority
        if embedded_wrong or cross_solver_wrong:
            result[name] = Cell("wrong", row["answer"], seconds)
        elif name in solved_names:
            status = "unique" if len(solved_names) == 1 else ("win" if seconds == best_time else "solved")
            result[name] = Cell(status, row["answer"], seconds)
        elif row["answer"] == "unsupported":
            result[name] = Cell("unsupported", "unsupported", seconds)
        else:
            status = "timeout" if row["status"] == "timeout" else "error"
            result[name] = Cell(status, status, seconds)
    return result


def par2(rows: dict[str, dict[str, str]], timeout: float) -> float:
    total = 0.0
    for row in rows.values():
        if row["status"] == "ok" and row["answer"] in CONCRETE_ANSWERS:
            total += float(row["seconds"])
        else:
            total += 2 * timeout
    return total / len(rows) if rows else 0.0


STYLE = """
:root {
  --bg: #FAF8F5;
  --surface: #FFFFFF;
  --surface-alt: #F1EEE7;
  --text: #23221F;
  --text-muted: #6B6860;
  --border: #E4E1DA;
  --accent: #2F6F6D;
  /* Status chips: solid, high-contrast backgrounds with white text --
     the whole point of this table is fast visual scanning, so these
     stay the same across themes (a self-contained colored block reads
     fine regardless of page background) rather than being tinted/muted
     per theme like the page chrome above. */
  --status-win: #189B6C;        --status-win-fg: #ffffff;
  --status-solved: #3D7A5D;     --status-solved-fg: #ffffff;
  --status-unique: #0B79B5;     --status-unique-fg: #ffffff;
  --status-wrong: #C13B32;      --status-wrong-fg: #ffffff;
  --status-timeout: #B0631F;    --status-timeout-fg: #ffffff;
  --status-error: #9C3D6B;      --status-error-fg: #ffffff;
  --status-unsupported: #5748C4; --status-unsupported-fg: #ffffff;
  --status-notrun: #55524B;     --status-notrun-fg: #ffffff;
  --font-display: ui-sans-serif, "Segoe UI", "Helvetica Neue", Arial, sans-serif;
  --font-mono: ui-monospace, "SF Mono", "Cascadia Code", "Roboto Mono", Menlo, Consolas, monospace;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #15161A; --surface: #1D1F24; --surface-alt: #24262C;
    --text: #E7E5DF; --text-muted: #9A978F; --border: #2C2E33;
    --accent: #6FD6C9;
  }
}
:root[data-theme="dark"] {
  --bg: #15161A; --surface: #1D1F24; --surface-alt: #24262C;
  --text: #E7E5DF; --text-muted: #9A978F; --border: #2C2E33;
  --accent: #6FD6C9;
}
:root[data-theme="light"] {
  --bg: #FAF8F5; --surface: #FFFFFF; --surface-alt: #F1EEE7;
  --text: #23221F; --text-muted: #6B6860; --border: #E4E1DA;
  --accent: #2F6F6D;
}
* { box-sizing: border-box; }
.compare-page {
  background: var(--bg); color: var(--text); font-family: var(--font-display);
  padding: 1.75rem clamp(1rem, 4vw, 2.5rem); max-width: 1400px; margin: 0 auto;
}
.compare-page h1 { font-size: 1.5rem; font-weight: 650; letter-spacing: -0.01em; text-wrap: balance; margin: 0; }
.compare-page .subtitle { color: var(--text-muted); font-size: 0.88rem; margin-top: 0.3rem; }
.legend { display: flex; flex-wrap: wrap; gap: 0.45rem; margin: 1.1rem 0 1.4rem; }
.legend-chip { display: inline-flex; align-items: center; gap: 0.4rem; padding: 0.22rem 0.6rem; border-radius: 999px; font-size: 0.72rem; font-weight: 600; background: var(--surface-alt); color: var(--text-muted); }
.legend-dot { width: 0.5rem; height: 0.5rem; border-radius: 50%; flex: none; }
.summary { display: grid; grid-template-columns: repeat(auto-fit, minmax(170px,1fr)); gap: 0.7rem; margin-bottom: 1.4rem; }
.stat-card { background: var(--surface); border: 1px solid var(--border); border-radius: 10px; padding: 0.85rem 1rem; }
.stat-card .solver-name { font-weight: 650; font-size: 0.92rem; }
.stat-card .solved-frac { font-family: var(--font-mono); font-variant-numeric: tabular-nums; font-size: 1.35rem; margin-top: 0.2rem; }
.stat-card .bar-track { height: 6px; border-radius: 3px; background: var(--surface-alt); margin-top: 0.5rem; overflow: hidden; }
.stat-card .bar-fill { height: 100%; background: var(--accent); }
.stat-card .par2 { color: var(--text-muted); font-size: 0.72rem; margin-top: 0.35rem; font-variant-numeric: tabular-nums; }
.controls { margin-bottom: 0.7rem; display: flex; align-items: center; gap: 0.6rem; }
#search { font-family: var(--font-mono); font-size: 0.82rem; padding: 0.4rem 0.65rem; border-radius: 8px; border: 1px solid var(--border); background: var(--surface); color: var(--text); width: 320px; max-width: 100%; }
#search:focus-visible { outline: 2px solid var(--accent); outline-offset: 1px; }
#match-count { color: var(--text-muted); font-size: 0.78rem; }
.table-wrap { overflow-x: auto; border: 1px solid var(--border); border-radius: 10px; max-height: 78vh; overflow-y: auto; }
table { border-collapse: collapse; width: 100%; font-family: var(--font-mono); font-size: 0.78rem; }
thead th { position: sticky; top: 0; background: var(--surface); border-bottom: 1px solid var(--border); padding: 0.5rem 0.7rem; text-align: left; font-family: var(--font-display); font-weight: 650; font-size: 0.7rem; text-transform: uppercase; letter-spacing: 0.04em; color: var(--text-muted); z-index: 1; }
tr.family-row td { background: var(--surface-alt); font-family: var(--font-display); font-weight: 650; font-size: 0.7rem; text-transform: uppercase; letter-spacing: 0.05em; color: var(--text-muted); padding: 0.4rem 0.7rem; position: sticky; top: 2.1rem; }
tr.instance-row:hover td { background: var(--surface-alt); }
tbody td { padding: 0.35rem 0.7rem; border-bottom: 1px solid var(--border); white-space: nowrap; }
td.problem { color: var(--text); max-width: 380px; overflow: hidden; text-overflow: ellipsis; }
.chip { display: inline-flex; align-items: baseline; gap: 0.4rem; padding: 0.16rem 0.55rem; border-radius: 6px; font-variant-numeric: tabular-nums; font-weight: 600; }
.chip-win { background: var(--status-win); color: var(--status-win-fg); }
.chip-solved { background: var(--status-solved); color: var(--status-solved-fg); }
.chip-unique { background: var(--status-unique); color: var(--status-unique-fg); }
.chip-wrong { background: var(--status-wrong); color: var(--status-wrong-fg); }
.chip-timeout { background: var(--status-timeout); color: var(--status-timeout-fg); }
.chip-error { background: var(--status-error); color: var(--status-error-fg); }
.chip-unsupported { background: var(--status-unsupported); color: var(--status-unsupported-fg); }
.chip-notrun { background: var(--status-notrun); color: var(--status-notrun-fg); }
"""

SCRIPT = """
(function () {
  const input = document.getElementById('search');
  const rows = Array.from(document.querySelectorAll('tr.instance-row'));
  const familyRows = Array.from(document.querySelectorAll('tr.family-row'));
  const counter = document.getElementById('match-count');
  function apply() {
    const q = input.value.trim().toLowerCase();
    let visible = 0;
    rows.forEach((row) => {
      const match = !q || row.dataset.search.includes(q);
      row.style.display = match ? '' : 'none';
      if (match) visible += 1;
    });
    familyRows.forEach((row) => {
      let next = row.nextElementSibling;
      let anyVisible = false;
      while (next && !next.classList.contains('family-row')) {
        if (next.style.display !== 'none') { anyVisible = true; break; }
        next = next.nextElementSibling;
      }
      row.style.display = anyVisible ? '' : 'none';
    });
    counter.textContent = q ? `${visible} / ${rows.length} instances` : `${rows.length} instances`;
  }
  input.addEventListener('input', apply);
  apply();
})();
"""


def render_html(results_dir: Path, solver_names: list[str], title: str, timeout: float) -> str:
    data = {name: load_csv(results_dir / f"{name}.csv") for name in solver_names}
    all_files = sorted(set().union(*(d.keys() for d in data.values())))
    family_of: dict[str, str] = {}
    for name in solver_names:
        for file_path, row in data[name].items():
            family_of.setdefault(file_path, row.get("family", ""))
    all_files.sort(key=lambda f: (family_of.get(f, ""), f))
    total = len(all_files)

    solved_counts = {name: 0 for name in solver_names}
    win_counts = {name: 0 for name in solver_names}

    rows_html: list[str] = []
    current_family = None
    for file_path in all_files:
        family = family_of.get(file_path, "")
        if family != current_family:
            current_family = family
            n_in_family = sum(1 for f in all_files if family_of.get(f) == family)
            rows_html.append(
                f'<tr class="family-row"><td colspan="{len(solver_names) + 1}">'
                f"{html.escape(family)} &middot; {n_in_family} instances</td></tr>"
            )
        cells = {name: data[name].get(file_path) for name in solver_names}
        classified = classify_row(cells)
        cell_html = []
        for name in solver_names:
            cell = classified[name]
            if cell.status in ("win", "solved", "unique"):
                solved_counts[name] += 1
            if cell.status == "win":
                win_counts[name] += 1
            time_text = f"{cell.seconds:.2f}s" if cell.seconds is not None else "&mdash;"
            cell_html.append(
                f'<td><span class="chip chip-{cell.status}">'
                f"{html.escape(cell.label)} {time_text}</span></td>"
            )
        search_key = f"{family} {file_path}".lower()
        rows_html.append(
            f'<tr class="instance-row" data-search="{html.escape(search_key)}">'
            f'<td class="problem" title="{html.escape(file_path)}">{html.escape(Path(file_path).name)}</td>'
            + "".join(cell_html)
            + "</tr>"
        )

    legend_html = "".join(
        f'<span class="legend-chip"><span class="legend-dot" style="background:var(--status-{key})"></span>'
        f"{html.escape(STATUS_LABELS[key])}</span>"
        for key in LEGEND_ORDER
    )
    header_cells = "".join(f"<th>{html.escape(name)}</th>" for name in solver_names)

    summary_html = []
    for name in solver_names:
        pct = 100 * solved_counts[name] / total if total else 0.0
        score = par2(data[name], timeout)
        summary_html.append(
            f'<div class="stat-card"><div class="solver-name">{html.escape(name)}</div>'
            f'<div class="solved-frac">{solved_counts[name]}/{total} <span style="color:var(--text-muted); font-size:0.85rem;">({pct:.0f}%)</span></div>'
            f'<div class="bar-track"><div class="bar-fill" style="width:{pct:.1f}%"></div></div>'
            f'<div class="par2">PAR2 {score:.2f}s &middot; {win_counts[name]} fastest</div></div>'
        )

    return f"""<style>{STYLE}</style>
<div class="compare-page">
<h1>{html.escape(title)}</h1>
<div class="subtitle">{total} instances &middot; {len(solver_names)} solvers &middot; timeout {timeout:.0f}s</div>
<div class="legend">{legend_html}</div>
<div class="summary">{"".join(summary_html)}</div>
<div class="controls">
  <input id="search" type="text" placeholder="Filter by family or file name&hellip;">
  <span id="match-count"></span>
</div>
<div class="table-wrap">
<table>
<thead><tr><th>Problem</th>{header_cells}</tr></thead>
<tbody>
{"".join(rows_html)}
</tbody>
</table>
</div>
</div>
<script>{SCRIPT}</script>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("results_dir", type=Path)
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--solvers", nargs="+", default=None)
    parser.add_argument("--title", default=None)
    parser.add_argument("--timeout", type=float, default=10.0, help="Per-instance timeout in seconds, for PAR2 (default: 10.0).")
    args = parser.parse_args()

    solver_names = discover_solvers(args.results_dir, args.solvers)
    if not solver_names:
        raise SystemExit(f"no solver CSVs found in {args.results_dir}")

    title = args.title or f"Solver comparison: {args.results_dir.name}"
    out_path = args.out or args.results_dir / "compare.html"
    out_path.write_text(render_html(args.results_dir, solver_names, title, args.timeout))
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
