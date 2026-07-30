"""Cross-check solver answers from bench_qf_uf_full.py CSVs and summarize.

Written for the scrambled QF_LRA competition set, where the benchmarks carry
no ``(set-info :status)``: bench_qf_uf_full.py derives its ``expected`` column
from that field, so on a scrambled set every row reports ``wrong=false``
regardless of what the solver actually answered. A "0 wrong" summary there is
therefore not evidence of soundness -- the only available signal is solvers
*disagreeing with each other*, which is what this script looks for.

Also merges per-shard CSVs, since the scrambled runs shard the manifest.

Usage:
    python3 scripts/compare_solver_answers.py RESULT_DIR [--timeout SECONDS]

RESULT_DIR contains either ``<name>.csv`` or sharded ``<name>.shard-N.csv``
files in bench_qf_uf_full.py's per-file row format.
"""

import argparse
import collections
import csv
import pathlib
import sys

DECISIVE = {"sat", "unsat"}


def load_solver_rows(directory: pathlib.Path) -> dict[str, dict[str, dict[str, str]]]:
    """Return {solver_name: {file: row}}, merging any shard CSVs."""
    solvers: dict[str, dict[str, dict[str, str]]] = collections.defaultdict(dict)
    for path in sorted(directory.glob("*.csv")):
        stem = path.stem
        if stem.endswith("-summary"):
            continue
        name = stem.split(".shard-")[0]
        with path.open() as handle:
            for row in csv.DictReader(handle):
                solvers[name][row["file"]] = row
    return solvers


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    solvers = load_solver_rows(args.directory)
    if not solvers:
        print(f"no solver CSVs found in {args.directory}", file=sys.stderr)
        return 1

    names = sorted(solvers)
    every_file = sorted({f for rows in solvers.values() for f in rows})

    print(f"{'solver':10s} {'files':>6} {'solved':>7} {'sat':>6} {'unsat':>6} "
          f"{'timeout':>8} {'error':>6} {'PAR2':>10}")
    for name in names:
        rows = solvers[name]
        answers = collections.Counter(r["answer"] for r in rows.values())
        solved = sum(1 for r in rows.values() if r["status"] == "ok")
        # Count timeouts by status, not by answer: a solver killed at the
        # limit reports answer="unknown" (Primo, which prints it and exits)
        # or answer="error" (z3/cvc5, killed by SIGTERM). Keying off the
        # answer column made the reference solvers look like they never timed
        # out at all.
        timeouts = sum(1 for r in rows.values() if r["status"] == "timeout")
        par2 = sum(
            float(r["seconds"]) if r["status"] == "ok" else 2 * args.timeout
            for r in rows.values()
        )
        errors = sum(1 for r in rows.values() if r["status"] not in {"ok", "timeout"})
        print(f"{name:10s} {len(rows):6d} {solved:7d} {answers['sat']:6d} "
              f"{answers['unsat']:6d} {timeouts:8d} {errors:6d} {par2:10.1f}")

    # The point of the script: a file where two solvers both answered
    # decisively and disagreed. On a set with no :status this is the only
    # soundness signal available.
    print("\n=== cross-solver disagreements ===")
    disagreements = 0
    for path in every_file:
        verdicts = {
            name: solvers[name][path]["answer"]
            for name in names
            if path in solvers[name] and solvers[name][path]["answer"] in DECISIVE
        }
        if len(set(verdicts.values())) > 1:
            disagreements += 1
            print(f"  {path}")
            for name, answer in sorted(verdicts.items()):
                print(f"      {name}: {answer}")
    print(f"total: {disagreements}")

    # Files nobody solved are the honest remaining-gap measure; files only one
    # solver got are where it is uniquely ahead.
    print("\n=== coverage ===")
    unsolved_by_all = [
        path
        for path in every_file
        if all(
            path not in solvers[name] or solvers[name][path]["status"] != "ok"
            for name in names
        )
    ]
    print(f"solved by nobody: {len(unsolved_by_all)}")
    for name in names:
        others = [n for n in names if n != name]
        only = [
            path
            for path in every_file
            if path in solvers[name]
            and solvers[name][path]["status"] == "ok"
            and all(
                path not in solvers[other] or solvers[other][path]["status"] != "ok"
                for other in others
            )
        ]
        print(f"solved only by {name}: {len(only)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
