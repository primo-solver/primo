"""Summarize a Slurm fuzz-gate result tree into per-fuzzer pass/fail counts.

Reconstructed by hand in several past sessions from scattered `find`/`csv`
one-liners; checked in so gate results are read the same way every time.

Reports the number of `summary.csv` files alongside the case counts on
purpose. A shard killed by the Slurm wall clock is terminated *before*
fuzz_external.py writes its summary, so a truncated gate reports fewer cases
and otherwise looks perfectly clean -- exactly how jobs 117248-117251 each
lost a shard and still read as 0 crashes / 0 wrong answers. Always check
`summaries` against the shard count before believing a result.

Usage:
    python3 scripts/summarize_fuzz_results.py RESULT_DIR [RESULT_DIR ...]
"""

import collections
import csv
import os
import sys

for root in sys.argv[1:]:
    print(f"=== {root} ===")
    for kind in ("fuzzsmt", "opfuzz", "yinyang"):
        base = os.path.join(root, kind)
        if not os.path.isdir(base):
            continue
        outcomes = collections.Counter()
        summaries = 0
        bugs = []
        for dirpath, _dirnames, filenames in os.walk(base):
            if "summary.csv" in filenames:
                summaries += 1
                with open(os.path.join(dirpath, "summary.csv")) as handle:
                    for row in csv.DictReader(handle):
                        outcomes[row.get("outcome", row.get("status", "?"))] += 1
            if os.path.basename(dirpath) == "bugs":
                bugs.extend(os.path.join(dirpath, f) for f in filenames)
        print(f"  {kind}: summaries={summaries} cases={sum(outcomes.values())} "
              f"{dict(outcomes)} bug_files={len(bugs)}")
        for path in bugs[:5]:
            print(f"    bug file: {path}")
