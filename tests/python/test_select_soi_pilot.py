from __future__ import annotations

import importlib.util
import unittest
from collections import Counter
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "select_soi_pilot",
    Path(__file__).resolve().parents[2] / "scripts/select_soi_pilot.py",
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PilotSelectionTests(unittest.TestCase):
    def test_selection_is_balanced_and_independent_of_csv_order(self):
        rows = [
            {
                "file": f"{family}/{number}.smt2",
                "parent": f"parent-{number}",
                "family": family,
                "expected": "sat",
                "stratum": "32-127",
                "successful_repeats": "3",
                "fallbacks": "0",
            }
            for family in ("a", "b", "c", "d")
            for number in range(10)
        ]
        selected = module.select(rows, 20261102, count=8)
        self.assertEqual(
            selected, module.select(list(reversed(rows)), 20261102, count=8)
        )
        self.assertEqual(
            Counter(row["stratum"][0] for row in selected), dict.fromkeys("abcd", 2)
        )
        self.assertEqual(len({row["file"] for row in selected}), 8)
