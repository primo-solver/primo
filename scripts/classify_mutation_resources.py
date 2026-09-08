#!/usr/bin/env python3
"""Permit diagnostic collection after resource-only incomplete mutation checks.

Exit zero means collection may continue, NOT that the gate passed. The caller
must retain the original nonzero stage status in its final qualification result.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def resource_only(rows: list[dict[str, str]], kind: str) -> bool:
    if not rows:
        return False
    incomplete = False
    for row in rows:
        outcome = row.get("outcome")
        if kind == "answers":
            if outcome == "PASS":
                continue
            if outcome != "INCOMPLETE":
                return False
            answers = [row.get(arm) for arm in ("ours", "reference")]
            if not any(answer in {"timeout", "unknown"} for answer in answers):
                return False
            for arm in ("ours", "reference"):
                answer = row.get(arm)
                expected_exit = "124" if answer == "timeout" else "0"
                if answer not in {"sat", "unsat", "unknown", "timeout"}:
                    return False
                if row.get(f"{arm}_exit") != expected_exit:
                    return False
        elif kind == "models":
            if outcome in {"pass", "not-sat"}:
                continue
            if outcome not in {"solver-timeout", "solver-unknown",
                               "dolmen-timeout", "dolmen-resource-limit"}:
                return False
        else:
            return False
        incomplete = True
    return incomplete


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("answers", "models"))
    parser.add_argument("summary", type=Path)
    args = parser.parse_args()
    with args.summary.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    permitted = resource_only(rows, args.kind)
    print("RESOURCE-ONLY INCOMPLETE: continue collection, retain failed gate status"
          if permitted else "not a resource-only incomplete result; stop")
    return 0 if permitted else 1


if __name__ == "__main__":
    raise SystemExit(main())
