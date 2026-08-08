"""Summarize a Slurm fuzz-gate result tree into per-layout pass/fail counts.

Reconstructed by hand in several past sessions from scattered `find`/`csv`
one-liners; checked in so gate results are read the same way every time.

Reports the number of `summary.csv` files alongside the case counts on
purpose. A shard killed by the Slurm wall clock is terminated *before*
fuzz_external.py writes its summary, so a truncated gate reports fewer cases
and otherwise looks perfectly clean -- exactly how jobs 117248-117251 each
lost a shard and still read as 0 crashes / 0 wrong answers. Always check
`summaries` against the shard count before believing a result.

The `fuzzsmt`, `opfuzz`, and `yinyang` subtrees are reported separately. Root
summaries and layouts such as the QF_UFLRA gate's `task-*` directories are
reported as `direct`.

Usage:
    python3 scripts/summarize_fuzz_results.py RESULT_DIR [RESULT_DIR ...]
"""

import collections
import csv
import sys
from pathlib import Path

KNOWN_KINDS = ("fuzzsmt", "opfuzz", "yinyang")


def result_kind(root: Path, path: Path) -> str:
    relative = path.relative_to(root)
    return relative.parts[0] if relative.parts[0] in KNOWN_KINDS else "direct"


for root_arg in sys.argv[1:]:
    root = Path(root_arg)
    print(f"=== {root} ===")
    outcomes_by_kind: dict[str, collections.Counter[str]] = {}
    summaries_by_kind: collections.Counter[str] = collections.Counter()
    bugs_by_kind: dict[str, list[Path]] = collections.defaultdict(list)

    for summary in root.rglob("summary.csv"):
        kind = result_kind(root, summary)
        summaries_by_kind[kind] += 1
        outcomes = outcomes_by_kind.setdefault(kind, collections.Counter())
        with summary.open(newline="") as handle:
            for row in csv.DictReader(handle):
                outcomes[row.get("outcome", row.get("status", "?"))] += 1

    for bugs_dir in root.rglob("bugs"):
        if not bugs_dir.is_dir():
            continue
        kind = result_kind(root, bugs_dir)
        bugs_by_kind[kind].extend(path for path in bugs_dir.iterdir() if path.is_file())

    kinds = [kind for kind in KNOWN_KINDS if kind in summaries_by_kind or kind in bugs_by_kind]
    if "direct" in summaries_by_kind or "direct" in bugs_by_kind:
        kinds.append("direct")
    for kind in kinds:
        outcomes = outcomes_by_kind.get(kind, collections.Counter())
        bugs = bugs_by_kind.get(kind, [])
        print(
            f"  {kind}: summaries={summaries_by_kind[kind]} cases={sum(outcomes.values())} "
            f"{dict(outcomes)} bug_files={len(bugs)}"
        )
        for path in bugs[:5]:
            print(f"    bug file: {path}")
