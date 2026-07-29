#!/usr/bin/env python3
"""Render benchmark CSVs as a compact detailed-results HTML matrix.

The input CSVs are the standard Primo benchmark outputs, or compatible files
with at least ``file``, ``status``, ``answer``, ``seconds``, and ``exit_code``
columns.  Pass each solver/configuration as ``--variant label=path/to.csv``.
"""

from __future__ import annotations

import argparse
import csv
import html
import json
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


CHECKABLE_ANSWERS = {"sat", "unsat"}
DEFAULT_STATS = [
    ":sat.conflicts",
    ":sat.decisions",
    ":sat.propagations",
    ":sat.clauses",
    ":sat.vars",
    ":euf.theory-conflicts",
    ":euf.incremental.conflicts",
    ":euf.incremental.explain-bfs-searches",
    ":euf.incremental.explain-bfs-visited",
    ":euf.incremental.explain-calls",
    ":euf.incremental.merges",
    ":euf.incremental.disequality-checks",
    ":euf.propagation.delivered",
    ":euf.propagation.reason-lits",
    ":euf.diseq-ack.candidates",
    ":euf.diseq-ack.propagated",
    ":euf.diseq-ack.unit",
    ":euf.diseq-ack.conflict",
    ":euf.diseq-ack.queue-pops",
    ":euf.diseq-ack.queue-pushes",
    ":euf.diseq-ack.reason-lits",
]


@dataclass(frozen=True)
class Variant:
    label: str
    csv_path: Path
    rows: dict[str, dict[str, str]]
    order: list[str]


def parse_variant(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("expected LABEL=CSV")
    label, csv_path = value.split("=", 1)
    label = label.strip()
    if not label:
        raise argparse.ArgumentTypeError("variant label must not be empty")
    return label, Path(csv_path)


def read_variant(label: str, csv_path: Path) -> Variant:
    if not csv_path.is_file():
        raise SystemExit(f"missing CSV for {label}: {csv_path}")
    rows: dict[str, dict[str, str]] = {}
    order: list[str] = []
    with csv_path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        fieldnames = set(reader.fieldnames or [])
        required = {"file", "status", "answer", "seconds"}
        missing = sorted(required - fieldnames)
        if missing:
            raise SystemExit(f"{csv_path}: missing required columns: {', '.join(missing)}")
        for index, row in enumerate(reader, start=2):
            file_name = row.get("file", "").strip()
            if not file_name:
                raise SystemExit(f"{csv_path}:{index}: empty file column")
            if file_name in rows:
                raise SystemExit(f"{csv_path}:{index}: duplicate file row: {file_name}")
            rows[file_name] = {key: value for key, value in row.items() if key is not None}
            order.append(file_name)
    return Variant(label=label, csv_path=csv_path, rows=rows, order=order)


def as_float(value: str | None, default: float = 0.0) -> float:
    try:
        return float(value or "")
    except ValueError:
        return default


def as_int(value: str | None) -> int:
    try:
        return int(float(value or "0"))
    except ValueError:
        return 0


def is_wrong(row: dict[str, str] | None) -> bool:
    if not row:
        return False
    return row.get("wrong", "").lower() == "true"


def is_solved(row: dict[str, str] | None) -> bool:
    if not row or is_wrong(row):
        return False
    return row.get("status") == "ok" and row.get("answer") in CHECKABLE_ANSWERS


def status_class(row: dict[str, str] | None) -> str:
    if row is None:
        return "not-run"
    if is_wrong(row):
        return "wrong"
    status = row.get("status", "")
    answer = row.get("answer", "")
    if status == "timeout":
        return "timeout"
    if status == "error":
        return "error"
    if status == "ok" and answer in CHECKABLE_ANSWERS:
        return "solved"
    return "unknown"


def display_name(path: str, strip_prefixes: Iterable[str], strip_suffixes: Iterable[str]) -> str:
    result = path
    for prefix in strip_prefixes:
        if prefix and result.startswith(prefix):
            result = result[len(prefix) :]
            break
    for suffix in strip_suffixes:
        if suffix and result.endswith(suffix):
            result = result[: -len(suffix)]
            break
    return result


def family_of(path: str, row: dict[str, str] | None) -> str:
    if row and row.get("family"):
        return row["family"]
    parts = Path(path).parts
    if "QF_UF" in parts:
        qfuf_index = parts.index("QF_UF")
        if qfuf_index + 1 < len(parts):
            family = parts[qfuf_index + 1]
            if family == "2018-Goel-hwbench":
                return "Goel"
            if family == "QG-classification":
                return "QG"
            return family
    return parts[0] if parts else ""


def cell_text(row: dict[str, str] | None, stat_keys: list[str]) -> str:
    if row is None:
        return '<span class="answer not-run">not-run</span>'
    seconds = as_float(row.get("seconds"))
    status = row.get("status", "")
    answer = row.get("answer", "")
    exit_code = row.get("exit_code", "")
    if status == "timeout":
        result = "n"
    elif status == "error":
        result = "e"
    elif status == "ok" and answer in CHECKABLE_ANSWERS:
        result = "s"
    else:
        result = "?"

    bits = [
        f'<div class="time">{seconds:.3f} (s)</div>',
        (
            f'<div class="answer {html.escape(status_class(row))}">'
            f"{html.escape(result)} {html.escape(answer)} (EC:{html.escape(exit_code)})"
            "</div>"
        ),
    ]
    stats = []
    for key in stat_keys:
        value = as_int(row.get(key))
        if value:
            stats.append(f"{short_stat_name(key)} {format_int(value)}")
    if stats:
        bits.append(f'<div class="stats">{html.escape("; ".join(stats))}</div>')
    return "\n".join(bits)


def short_stat_name(key: str) -> str:
    replacements = {
        ":sat.conflicts": "sat-conf",
        ":sat.decisions": "sat-dec",
        ":sat.propagations": "sat-prop",
        ":sat.clauses": "clauses",
        ":sat.vars": "vars",
        ":euf.theory-conflicts": "th-conf",
        ":euf.incremental.conflicts": "euf-conf",
        ":euf.incremental.explain-bfs-searches": "bfs",
        ":euf.incremental.explain-bfs-visited": "bfs-vis",
        ":euf.incremental.explain-calls": "explain",
        ":euf.incremental.merges": "merges",
        ":euf.incremental.disequality-checks": "diseq-checks",
        ":euf.propagation.delivered": "euf-prop",
        ":euf.propagation.reason-lits": "prop-reason",
        ":euf.diseq-ack.candidates": "ack-cand",
        ":euf.diseq-ack.propagated": "ack-prop",
        ":euf.diseq-ack.unit": "ack-unit",
        ":euf.diseq-ack.conflict": "ack-conf",
        ":euf.diseq-ack.queue-pops": "ack-pop",
        ":euf.diseq-ack.queue-pushes": "ack-push",
        ":euf.diseq-ack.reason-lits": "ack-reason",
    }
    return replacements.get(key, key.removeprefix(":"))


def format_int(value: int) -> str:
    return f"{value:,}"


def format_float(value: float) -> str:
    return f"{value:.3f}"


def summarize_variant(
    variant: Variant,
    all_files: list[str],
    timeout: float | None,
    wins: Counter[str],
    uniques: Counter[str],
    suboptimal: Counter[str],
) -> dict[str, object]:
    rows = [variant.rows.get(file_name) for file_name in all_files]
    solved = sum(1 for row in rows if is_solved(row))
    wrong = sum(1 for row in rows if is_wrong(row))
    out_err = sum(
        1 for row in rows if row is not None and row.get("status") in {"timeout", "error"}
    )
    not_run = sum(1 for row in rows if row is None)
    seconds = sum(as_float(row.get("seconds")) for row in rows if row is not None)
    par2 = None
    if timeout is not None:
        par2 = sum(
            as_float(row.get("seconds"))
            if is_solved(row)
            else 2.0 * timeout
            for row in rows
        )
    return {
        "label": variant.label,
        "total": len(all_files),
        "solved": solved,
        "out_err": out_err,
        "wrong": wrong,
        "not_run": not_run,
        "seconds": seconds,
        "par2": par2,
        "win": wins[variant.label],
        "unique": uniques[variant.label],
        "suboptimal": suboptimal[variant.label],
    }


def compute_competition_counts(
    variants: list[Variant],
    all_files: list[str],
    suboptimal_epsilon: float,
) -> tuple[Counter[str], Counter[str], Counter[str]]:
    wins: Counter[str] = Counter()
    uniques: Counter[str] = Counter()
    suboptimal: Counter[str] = Counter()
    for file_name in all_files:
        solved = [
            (variant.label, as_float(variant.rows[file_name].get("seconds")))
            for variant in variants
            if file_name in variant.rows and is_solved(variant.rows[file_name])
        ]
        if not solved:
            continue
        best = min(seconds for _, seconds in solved)
        best_labels = [label for label, seconds in solved if abs(seconds - best) <= 1e-9]
        for label in best_labels:
            wins[label] += 1
        if len(solved) == 1:
            uniques[solved[0][0]] += 1
        for label, seconds in solved:
            if seconds > best + suboptimal_epsilon:
                suboptimal[label] += 1
    return wins, uniques, suboptimal


def select_stats(
    variants: list[Variant],
    requested: list[str],
    include_defaults: bool,
) -> list[str]:
    wanted = list(DEFAULT_STATS if include_defaults else [])
    wanted.extend(requested)
    available = {
        key
        for variant in variants
        for row in variant.rows.values()
        for key, value in row.items()
        if key.startswith(":") and as_int(value) != 0
    }
    seen: set[str] = set()
    selected: list[str] = []
    for key in wanted:
        if key in seen:
            continue
        seen.add(key)
        if key in available or key in requested:
            selected.append(key)
    return selected


def write_html(
    path: Path,
    title: str,
    variants: list[Variant],
    all_files: list[str],
    strip_prefixes: list[str],
    strip_suffixes: list[str],
    stat_keys: list[str],
    timeout: float | None,
    metadata: dict[str, str],
    suboptimal_epsilon: float,
) -> None:
    wins, uniques, suboptimal = compute_competition_counts(
        variants, all_files, suboptimal_epsilon
    )
    summaries = [
        summarize_variant(variant, all_files, timeout, wins, uniques, suboptimal)
        for variant in variants
    ]
    families = sorted(
        {
            family_of(
                file_name,
                next(
                    (
                        variant.rows.get(file_name)
                        for variant in variants
                        if file_name in variant.rows
                    ),
                    None,
                ),
            )
            for file_name in all_files
        }
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        handle.write("<!doctype html>\n<html lang=\"en\">\n<head>\n")
        handle.write("<meta charset=\"utf-8\">\n")
        handle.write(f"<title>{html.escape(title)}</title>\n")
        handle.write(
            "<style>\n"
            ":root{color-scheme:light;--grid:#cbd5e1;--head:#e2e8f0;--muted:#475569;"
            "--solved:#ecfdf5;--timeout:#fff7ed;--error:#fef2f2;--wrong:#fee2e2;"
            "--unknown:#f8fafc;--notrun:#f1f5f9;--win:#dcfce7;--loss:#fee2e2;}"
            "body{font-family:Arial,Helvetica,sans-serif;margin:16px;color:#0f172a;"
            "background:#fff;}"
            "h1{font-size:24px;margin:0 0 8px;}h2{font-size:18px;margin:22px 0 8px;}"
            ".meta{color:var(--muted);font-size:13px;margin-bottom:12px;}"
            ".legend{font-family:monospace;font-size:13px;margin:10px 0 14px;white-space:pre-wrap;}"
            "table{border-collapse:collapse;width:100%;font-size:13px;}"
            "th,td{border:1px solid var(--grid);padding:5px 6px;vertical-align:top;}"
            "th{background:var(--head);position:sticky;top:0;z-index:1;text-align:left;}"
            ".num{text-align:right;font-variant-numeric:tabular-nums;}"
            ".problem{font-family:monospace;white-space:nowrap;}"
            ".cell{min-width:150px;max-width:220px;}"
            ".time{font-family:monospace;font-weight:700;}"
            ".answer{font-family:monospace;margin-top:2px;}"
            ".stats{color:var(--muted);font-size:11px;line-height:1.35;margin-top:3px;}"
            ".solved{background:var(--solved);} .timeout{background:var(--timeout);}"
            ".error{background:var(--error);} .wrong{background:var(--wrong);font-weight:700;}"
            ".unknown{background:var(--unknown);} .not-run{background:var(--notrun);"
            "color:var(--muted);}"
            ".delta.win{background:var(--win);} .delta.loss{background:var(--loss);}"
            ".wrap{overflow:auto;max-height:80vh;border:1px solid var(--grid);}"
            ".small{font-size:12px;color:var(--muted);}"
            "</style>\n"
        )
        handle.write("</head>\n<body>\n")
        handle.write(f"<h1>{html.escape(title)}</h1>\n")
        if metadata:
            handle.write('<div class="meta">')
            handle.write(
                " | ".join(
                    f"{html.escape(key)}: {html.escape(value)}"
                    for key, value in metadata.items()
                    if value
                )
            )
            handle.write("</div>\n")
        handle.write(
            '<div class="legend">TIME OUT/ERR | WRONG | SOLVED | WIN | UNIQUE | '
            "SUBOPTIMAL | NOT-RUN</div>\n"
        )
        handle.write("<h2>Summary</h2>\n<table>\n<thead><tr>")
        summary_headers = [
            "Variant",
            "Total",
            "Solved",
            "Out/Err",
            "Wrong",
            "Win",
            "Unique",
            "Suboptimal",
            "Not-run",
            "Time (s)",
        ]
        if timeout is not None:
            summary_headers.append("PAR2")
        for header in summary_headers:
            handle.write(f"<th>{html.escape(header)}</th>")
        handle.write("</tr></thead>\n<tbody>\n")
        for summary in summaries:
            handle.write("<tr>")
            handle.write(f"<td>{html.escape(str(summary['label']))}</td>")
            for key in [
                "total",
                "solved",
                "out_err",
                "wrong",
                "win",
                "unique",
                "suboptimal",
                "not_run",
            ]:
                handle.write(f'<td class="num">{summary[key]}</td>')
            handle.write(f'<td class="num">{format_float(float(summary["seconds"]))}</td>')
            if timeout is not None:
                handle.write(f'<td class="num">{format_float(float(summary["par2"]))}</td>')
            handle.write("</tr>\n")
        handle.write("</tbody>\n</table>\n")

        handle.write("<h2>Families</h2>\n<table>\n<thead><tr><th>Family</th>")
        for variant in variants:
            handle.write(f"<th>{html.escape(variant.label)} solved</th>")
            handle.write(f"<th>{html.escape(variant.label)} time</th>")
        handle.write("</tr></thead>\n<tbody>\n")
        for family in families:
            family_files = [
                file_name
                for file_name in all_files
                if family_of(
                    file_name,
                    next(
                        (
                            variant.rows.get(file_name)
                            for variant in variants
                            if file_name in variant.rows
                        ),
                        None,
                    ),
                )
                == family
            ]
            handle.write(f"<tr><td>{html.escape(family)}</td>")
            for variant in variants:
                rows = [variant.rows.get(file_name) for file_name in family_files]
                solved = sum(1 for row in rows if is_solved(row))
                seconds = sum(as_float(row.get("seconds")) for row in rows if row is not None)
                handle.write(f'<td class="num">{solved}/{len(family_files)}</td>')
                handle.write(f'<td class="num">{format_float(seconds)}</td>')
            handle.write("</tr>\n")
        handle.write("</tbody>\n</table>\n")

        if len(variants) == 2:
            handle.write("<h2>Time Deltas</h2>\n<table>\n<thead><tr>")
            for header in [
                "Problem",
                variants[0].label,
                variants[1].label,
                f"{variants[1].label} - {variants[0].label}",
            ]:
                handle.write(f"<th>{html.escape(header)}</th>")
            handle.write("</tr></thead>\n<tbody>\n")
            delta_rows: list[tuple[float, str, dict[str, str] | None, dict[str, str] | None]] = []
            for file_name in all_files:
                left = variants[0].rows.get(file_name)
                right = variants[1].rows.get(file_name)
                if left is None or right is None:
                    continue
                delta = as_float(right.get("seconds")) - as_float(left.get("seconds"))
                delta_rows.append((abs(delta), file_name, left, right))
            for _, file_name, left, right in sorted(delta_rows, reverse=True)[:25]:
                delta = as_float(right.get("seconds")) - as_float(left.get("seconds"))
                cls = "win" if delta < 0 else "loss" if delta > 0 else ""
                handle.write("<tr>")
                problem = display_name(file_name, strip_prefixes, strip_suffixes)
                handle.write(
                    f'<td class="problem">{html.escape(problem)}</td>'
                )
                handle.write(f'<td class="num">{format_float(as_float(left.get("seconds")))}</td>')
                handle.write(f'<td class="num">{format_float(as_float(right.get("seconds")))}</td>')
                handle.write(f'<td class="num delta {cls}">{format_float(delta)}</td>')
                handle.write("</tr>\n")
            handle.write("</tbody>\n</table>\n")

        handle.write("<h2>Detailed Results</h2>\n<div class=\"wrap\"><table>\n<thead><tr>")
        handle.write("<th>Problem</th>")
        for variant in variants:
            handle.write(f"<th>{html.escape(variant.label)}</th>")
        handle.write("</tr></thead>\n<tbody>\n")
        for file_name in all_files:
            handle.write("<tr>")
            problem = display_name(file_name, strip_prefixes, strip_suffixes)
            handle.write(f'<td class="problem">{html.escape(problem)}</td>')
            for variant in variants:
                row = variant.rows.get(file_name)
                handle.write(
                    f'<td class="cell {status_class(row)}">'
                    f"{cell_text(row, stat_keys)}</td>"
                )
            handle.write("</tr>\n")
        handle.write("</tbody>\n</table></div>\n")

        if stat_keys:
            handle.write("<h2>Stat Totals</h2>\n<table>\n<thead><tr><th>Stat</th>")
            for variant in variants:
                handle.write(f"<th>{html.escape(variant.label)} total</th>")
                handle.write(f"<th>{html.escape(variant.label)} active rows</th>")
            handle.write("</tr></thead>\n<tbody>\n")
            for key in stat_keys:
                handle.write(f"<tr><td>{html.escape(key)}</td>")
                for variant in variants:
                    total = sum(as_int(row.get(key)) for row in variant.rows.values())
                    active = sum(1 for row in variant.rows.values() if as_int(row.get(key)) != 0)
                    handle.write(f'<td class="num">{format_int(total)}</td>')
                    handle.write(f'<td class="num">{active}</td>')
                handle.write("</tr>\n")
            handle.write("</tbody>\n</table>\n")

        handle.write(
            '<p class="small">Cell codes: s = solved sat/unsat, n = timeout, '
            "e = process error, ? = unknown/unsupported/other answer. "
            "Wrong answers are computed from the CSV wrong column when present.</p>\n"
        )
        handle.write("</body>\n</html>\n")


def write_summary_json(
    path: Path,
    title: str,
    variants: list[Variant],
    all_files: list[str],
    timeout: float | None,
    suboptimal_epsilon: float,
) -> None:
    wins, uniques, suboptimal = compute_competition_counts(
        variants, all_files, suboptimal_epsilon
    )
    data = {
        "title": title,
        "timeout_seconds": timeout,
        "variants": [
            summarize_variant(variant, all_files, timeout, wins, uniques, suboptimal)
            for variant in variants
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--variant",
        action="append",
        type=parse_variant,
        required=True,
        metavar="LABEL=CSV",
        help="benchmark CSV for one solver/configuration; repeat for a matrix",
    )
    parser.add_argument("--out", type=Path, required=True, help="HTML output path")
    parser.add_argument("--title", default="Benchmark Detailed Results")
    parser.add_argument("--timeout", type=float, help="per-file timeout for PAR2")
    parser.add_argument(
        "--stat",
        action="append",
        default=[],
        help="additional CSV stat column to show in cells and totals",
    )
    parser.add_argument(
        "--no-default-stats",
        action="store_true",
        help="only show stats explicitly named with --stat",
    )
    parser.add_argument(
        "--strip-prefix",
        action="append",
        default=[],
        help="strip this prefix from problem names in the HTML",
    )
    parser.add_argument(
        "--strip-suffix",
        action="append",
        default=[],
        help="strip this suffix from problem names in the HTML",
    )
    parser.add_argument(
        "--metadata",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="metadata shown under the title; repeat as needed",
    )
    parser.add_argument(
        "--summary-json",
        type=Path,
        help="optional compact machine-readable summary output",
    )
    parser.add_argument(
        "--suboptimal-epsilon",
        type=float,
        default=1.0,
        help="minimum solved-time gap, in seconds, counted as suboptimal",
    )
    args = parser.parse_args()

    variants = [read_variant(label, csv_path) for label, csv_path in args.variant]
    labels = [variant.label for variant in variants]
    if len(set(labels)) != len(labels):
        raise SystemExit("variant labels must be unique")

    all_files: list[str] = []
    seen: set[str] = set()
    for variant in variants:
        for file_name in variant.order:
            if file_name not in seen:
                seen.add(file_name)
                all_files.append(file_name)

    metadata: dict[str, str] = {}
    for item in args.metadata:
        if "=" not in item:
            raise SystemExit(f"--metadata expects KEY=VALUE, got: {item}")
        key, value = item.split("=", 1)
        metadata[key] = value

    stat_keys = select_stats(variants, args.stat, not args.no_default_stats)
    write_html(
        args.out,
        args.title,
        variants,
        all_files,
        args.strip_prefix,
        args.strip_suffix,
        stat_keys,
        args.timeout,
        metadata,
        args.suboptimal_epsilon,
    )
    if args.summary_json:
        write_summary_json(
            args.summary_json,
            args.title,
            variants,
            all_files,
            args.timeout,
            args.suboptimal_epsilon,
        )
    print(f"wrote {args.out}")
    if args.summary_json:
        print(f"wrote {args.summary_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
