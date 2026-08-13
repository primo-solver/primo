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
import json
import sys
from pathlib import Path

KNOWN_KINDS = ("fuzzsmt", "opfuzz", "yinyang", "finite-domain", "model-validation")
failed = False


def result_kind(root: Path, path: Path) -> str:
    relative = path.relative_to(root)
    if "model-validation" in relative.parts:
        return "model-validation"
    if relative.parts[0] in KNOWN_KINDS:
        return relative.parts[0]
    if any(part.startswith("finite-") for part in relative.parts):
        return "finite-domain"
    return "direct"


def summary_kind(root: Path, path: Path) -> str:
    kind = result_kind(root, path)
    if kind != "direct":
        return kind
    with path.open(newline="") as handle:
        fields = set(csv.DictReader(handle).fieldnames or [])
    if {"validator", "dolmen_exit"}.issubset(fields):
        return "model-validation"
    return kind


for root_arg in sys.argv[1:]:
    root = Path(root_arg)
    print(f"=== {root} ===")
    outcomes_by_kind: dict[str, collections.Counter[str]] = {}
    summaries_by_kind: collections.Counter[str] = collections.Counter()
    bugs_by_kind: dict[str, list[Path]] = collections.defaultdict(list)
    scenarios_by_kind: dict[str, collections.Counter[str]] = collections.defaultdict(
        collections.Counter
    )
    activation_by_kind: dict[str, collections.Counter[str]] = collections.defaultdict(
        collections.Counter
    )
    validators_by_kind: dict[str, collections.Counter[str]] = collections.defaultdict(
        collections.Counter
    )
    metadata_by_kind: collections.Counter[str] = collections.Counter()
    complete_metadata_by_kind: collections.Counter[str] = collections.Counter()
    expected_cases_by_kind: collections.Counter[str] = collections.Counter()

    for summary in root.rglob("summary.csv"):
        kind = summary_kind(root, summary)
        summaries_by_kind[kind] += 1
        outcomes = outcomes_by_kind.setdefault(kind, collections.Counter())
        with summary.open(newline="") as handle:
            for row in csv.DictReader(handle):
                outcomes[row.get("outcome", row.get("status", "?"))] += 1
                if row.get("scenario"):
                    scenarios_by_kind[kind][row["scenario"]] += 1
                if row.get("validator"):
                    validators_by_kind[kind][row["validator"]] += 1
                for field in ("raw_amo", "post_amo", "amo", "real_lowered"):
                    try:
                        activation_by_kind[kind][field] += int(row.get(field, 0))
                    except ValueError:
                        pass

    for metadata_path in root.rglob("metadata.json"):
        with metadata_path.open(encoding="utf-8") as handle:
            metadata = json.load(handle)
        kind = result_kind(root, metadata_path)
        if kind == "direct" and "require_dolmen_pass" in metadata:
            kind = "model-validation"
        metadata_by_kind[kind] += 1
        expected_cases_by_kind[kind] += int(metadata.get("count", 0))
        if metadata.get("status") == "complete":
            complete_metadata_by_kind[kind] += 1
        if metadata.get("aggregate_activation_failure"):
            failed = True

    for bugs_dir in root.rglob("bugs"):
        if not bugs_dir.is_dir():
            continue
        kind = result_kind(root, bugs_dir)
        bugs_by_kind[kind].extend(path for path in bugs_dir.iterdir() if path.is_file())

    kinds = [
        kind
        for kind in KNOWN_KINDS
        if kind in summaries_by_kind or kind in bugs_by_kind
    ]
    if "direct" in summaries_by_kind or "direct" in bugs_by_kind:
        kinds.append("direct")
    for kind in kinds:
        outcomes = outcomes_by_kind.get(kind, collections.Counter())
        bugs = bugs_by_kind.get(kind, [])
        print(
            f"  {kind}: summaries={summaries_by_kind[kind]} cases={sum(outcomes.values())} "
            f"{dict(outcomes)} bug_files={len(bugs)}"
        )
        if validators_by_kind[kind]:
            print(f"    validators={dict(validators_by_kind[kind])}")
            if kind == "model-validation":
                failed = failed or any(
                    validator != "dolmen" and count
                    for validator, count in validators_by_kind[kind].items()
                )
        if metadata_by_kind[kind]:
            observed_cases = sum(outcomes.values())
            complete = complete_metadata_by_kind[kind] == metadata_by_kind[kind]
            count_matches = observed_cases == expected_cases_by_kind[kind]
            print(
                f"    metadata={complete_metadata_by_kind[kind]}/{metadata_by_kind[kind]} "
                f"expected_cases={expected_cases_by_kind[kind]} "
                f"scenarios={dict(scenarios_by_kind[kind])} "
                f"activation={dict(activation_by_kind[kind])}"
            )
            failed = failed or not complete or not count_matches
        accepted_outcomes = (
            {"pass", "not-sat"} if kind == "model-validation" else {"PASS", "TIMEOUT"}
        )
        failed = failed or any(
            outcomes[outcome] for outcome in outcomes if outcome not in accepted_outcomes
        )
        for path in bugs[:5]:
            print(f"    bug file: {path}")

raise SystemExit(1 if failed else 0)
