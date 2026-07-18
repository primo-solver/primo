#!/usr/bin/env python3
"""Build QF_UF train/test manifests for Primo irace tuning."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def normalize(path: str) -> str:
    for prefix in ("tmp/non-incremental/QF_UF/", "tmp/QF_UF/"):
        if prefix in path:
            return path.split(prefix, 1)[1]
    return path


def is_solved(row: dict[str, str]) -> bool:
    return row.get("status") == "ok" and row.get("answer") in {"sat", "unsat"}


def seconds(row: dict[str, str]) -> float:
    try:
        return float(row.get("seconds", "") or "0")
    except ValueError:
        return 0.0


def load(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="") as handle:
        return {
            normalize(row.get("relative_file") or row.get("file") or ""): row
            for row in csv.DictReader(handle)
        }


def benchmark_path(relative: str) -> str:
    return f"tmp/QF_UF/{relative}"


def existing(repo: Path, relatives: list[str]) -> list[str]:
    result = []
    for relative in relatives:
        if (repo / benchmark_path(relative)).is_file():
            result.append(relative)
    return result


def write_manifest(path: Path, relatives: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{benchmark_path(relative)}\n" for relative in relatives))


def interleave(*groups: list[str]) -> list[str]:
    result = []
    seen = set()
    max_len = max((len(group) for group in groups), default=0)
    for index in range(max_len):
        for group in groups:
            if index < len(group) and group[index] not in seen:
                seen.add(group[index])
                result.append(group[index])
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument(
        "--native",
        type=Path,
        default=Path(
            "results/slurm-qfuf-neq-peq-seq-vp-fixed-default-120s-110617/"
            "primo-qfuf-neq-peq-seq-vp-fixed-default-120s-merged.csv"
        ),
    )
    parser.add_argument(
        "--llm2smt",
        type=Path,
        default=Path(
            "results/slurm-qfuf-full-120s-llm2smt-0.2.4-110181/"
            "llm2smt-qfuf-full-120s-merged.csv"
        ),
    )
    parser.add_argument(
        "--train-out", type=Path, default=Path("benchmarks/qfuf-irace-train.txt")
    )
    parser.add_argument(
        "--test-out", type=Path, default=Path("benchmarks/qfuf-irace-test.txt")
    )
    parser.add_argument("--slow-ratio", type=float, default=5.0)
    parser.add_argument("--slow-margin", type=float, default=1.0)
    parser.add_argument("--max-slow", type=int, default=24)
    parser.add_argument("--max-guard", type=int, default=24)
    args = parser.parse_args()

    native = load(args.native)
    llm2smt = load(args.llm2smt)
    timeout_relatives = sorted(
        relative for relative, row in native.items() if not is_solved(row)
    )
    slow = []
    for relative, native_row in native.items():
        ref_row = llm2smt.get(relative)
        if not is_solved(native_row) or not is_solved(ref_row):
            continue
        native_seconds = seconds(native_row)
        ref_seconds = max(seconds(ref_row), 1e-6)
        if (
            native_seconds > args.slow_ratio * ref_seconds
            and native_seconds - ref_seconds > args.slow_margin
        ):
            slow.append((native_seconds / ref_seconds, relative))
    slow_relatives = [
        relative for _, relative in sorted(slow, reverse=True)[: args.max_slow]
    ]

    guards = []
    for relative, row in sorted(native.items(), key=lambda item: seconds(item[1])):
        if is_solved(row) and seconds(row) < 2.0:
            guards.append(relative)
        if len(guards) >= args.max_guard:
            break

    train = existing(args.repo, interleave(guards, slow_relatives, timeout_relatives))
    seen = set(train)
    test_candidates = timeout_relatives + [
        relative
        for _, relative in sorted(slow, reverse=True)[args.max_slow : args.max_slow * 2]
    ]
    remaining = [relative for relative in sorted(native) if relative not in seen]
    test = existing(
        args.repo, [relative for relative in test_candidates if relative not in seen]
    )
    for relative in remaining:
        if len(test) >= args.max_slow + args.max_guard:
            break
        if relative not in test and (args.repo / benchmark_path(relative)).is_file():
            test.append(relative)

    write_manifest(args.train_out, train)
    write_manifest(args.test_out, test)
    print(f"train {len(train)} {args.train_out}")
    print(f"test {len(test)} {args.test_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
