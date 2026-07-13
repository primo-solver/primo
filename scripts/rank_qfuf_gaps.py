#!/usr/bin/env python3
"""Rank QF_UF instances where references are fast and Primo is slow.

The input files are CSV/TSV benchmark outputs with at least ``file``,
``status``, ``answer``, and ``seconds`` columns.  Reference files may be passed
more than once; the fastest successful reference run is used for each instance.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


CHECKABLE = {"sat", "unsat"}


def dialect(path: Path) -> csv.Dialect:
  sample = path.read_text(errors="replace")[:4096]
  try:
    return csv.Sniffer().sniff(sample, delimiters=",\t")
  except csv.Error:
    return csv.excel_tab if path.suffix == ".tsv" else csv.excel


def read_rows(path: Path) -> dict[str, dict[str, str]]:
  with path.open(newline="") as handle:
    reader = csv.DictReader(handle, dialect=dialect(path))
    if reader.fieldnames is None:
      raise SystemExit(f"{path}: missing header")
    required = {"file", "status", "answer", "seconds"}
    missing = required.difference(reader.fieldnames)
    if missing:
      raise SystemExit(f"{path}: missing columns: {', '.join(sorted(missing))}")
    return {row["file"]: row for row in reader if row.get("file")}


def seconds(row: dict[str, str], timeout: float) -> float:
  try:
    return float(row.get("seconds", "") or timeout)
  except ValueError:
    return timeout


def is_solved(row: dict[str, str]) -> bool:
  return row.get("status") == "ok" and row.get("answer") in CHECKABLE


def family(row: dict[str, str], file: str) -> str:
  explicit = row.get("family")
  if explicit:
    return explicit
  parts = Path(file).parts
  if "QF_UF" in parts:
    index = parts.index("QF_UF")
    if index + 1 < len(parts):
      first = parts[index + 1]
      return "Goel" if first == "2018-Goel-hwbench" else first
  return parts[-2] if len(parts) > 1 else ""


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--native", type=Path, required=True)
  parser.add_argument("--ref", type=Path, action="append", required=True)
  parser.add_argument("--timeout", type=float, required=True)
  parser.add_argument("--fast-ref", type=float, default=1.0)
  parser.add_argument("--min-ratio", type=float, default=5.0)
  parser.add_argument("--top", type=int, default=50)
  parser.add_argument("--include-unsupported", action="store_true")
  parser.add_argument("--out", type=Path)
  args = parser.parse_args()

  native_rows = read_rows(args.native)
  ref_rows = [read_rows(path) for path in args.ref]
  ranked: list[dict[str, object]] = []

  for file, native in native_rows.items():
    best_ref: dict[str, str] | None = None
    best_ref_seconds = args.timeout
    for rows in ref_rows:
      ref = rows.get(file)
      if ref is None or not is_solved(ref):
        continue
      ref_seconds = seconds(ref, args.timeout)
      if ref_seconds < best_ref_seconds:
        best_ref = ref
        best_ref_seconds = ref_seconds
    if best_ref is None or best_ref_seconds > args.fast_ref:
      continue

    native_seconds = seconds(native, args.timeout)
    native_solved = is_solved(native)
    if native.get("answer") == "unsupported" and not args.include_unsupported:
      continue
    native_penalty = native_seconds if native_solved else 2.0 * args.timeout
    ratio = native_penalty / max(best_ref_seconds, 1e-9)
    wrong = native.get("wrong") == "true"
    if wrong or ratio < args.min_ratio:
      continue
    ranked.append(
        {
            "file": file,
            "family": family(native, file),
            "native_status": native.get("status", ""),
            "native_answer": native.get("answer", ""),
            "native_seconds": f"{native_seconds:.6f}",
            "ref_answer": best_ref.get("answer", ""),
            "ref_seconds": f"{best_ref_seconds:.6f}",
            "ratio": f"{ratio:.2f}",
            "native_solve_calls": native.get(":sat.solve-calls", ""),
            "native_theory_atoms": native.get(":euf.theory-atoms", ""),
            "native_theory_conflicts": native.get(":euf.theory-conflicts", ""),
            "native_amo": native.get(":preprocessing.finite-domain.amo-clauses", ""),
            "native_symmetry": native.get(":preprocessing.uf-symmetry.constraints", ""),
        }
    )

  ranked.sort(key=lambda row: (float(row["ratio"]), float(row["native_seconds"])), reverse=True)
  ranked = ranked[: args.top]
  fieldnames = [
      "file",
      "family",
      "native_status",
      "native_answer",
      "native_seconds",
      "ref_answer",
      "ref_seconds",
      "ratio",
      "native_solve_calls",
      "native_theory_atoms",
      "native_theory_conflicts",
      "native_amo",
      "native_symmetry",
  ]
  output = args.out.open("w", newline="") if args.out is not None else sys.stdout
  try:
    writer = csv.DictWriter(output, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(ranked)
  finally:
    if args.out is not None:
      output.close()
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
