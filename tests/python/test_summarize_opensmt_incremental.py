#!/usr/bin/env python3
"""Coverage and partial-answer safeguards for the added reference comparison."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[2] / "scripts"))
from summarize_opensmt_incremental import checked_rows, compare  # noqa: E402


class OpenSmtComparisonTest(unittest.TestCase):
    def row(self, answers, outcome="complete", seconds=2):
        return {"index": 0, "file": "case.smt2", "answers": answers,
                "outcome": outcome, "seconds": seconds}

    def test_partial_answers_are_checked_but_not_timed_as_complete(self):
        left = {0: self.row(["sat", "unknown"], "unknown")}
        right = {0: self.row(["unsat", "sat"])}
        result = compare(left, right)
        self.assertEqual(result["disagreements"],
                         [{"index": 0, "query": 0, "answers": ["sat", "unsat"]}])
        self.assertEqual(result["mutually_complete_sessions"], 0)
        self.assertIsNone(result["median_left_over_right_seconds"])
        self.assertEqual(result["only_left_complete"], [])
        self.assertEqual(result["only_right_complete"], [0])

    def test_complete_count_mismatch_is_rejected(self):
        with self.assertRaises(ValueError):
            compare({0: self.row(["sat"])}, {0: self.row(["sat", "sat"])})

    def test_mutually_complete_ratio(self):
        result = compare({0: self.row(["sat"], seconds=4)},
                         {0: self.row(["sat"], seconds=2)})
        self.assertEqual(result["median_left_over_right_seconds"], 2)
        self.assertEqual(result["disagreements"], [])

    def test_missing_coverage_and_errors_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shard = root / "shard-0"
            shard.mkdir()
            summary = shard / "summary.json"
            summary.write_text(json.dumps([self.row(["sat"])]))
            self.assertEqual(len(checked_rows(root, 1)), 1)
            with self.assertRaises(ValueError):
                checked_rows(root, 2)
            summary.write_text(json.dumps([self.row(["sat"], "error")]))
            with self.assertRaises(ValueError):
                checked_rows(root, 1)
            self.assertEqual(checked_rows(root, 1, allow_errors=True)[0]["outcome"], "error")
            summary.write_text(json.dumps([self.row(["sat"], "wrong-answer")]))
            with self.assertRaises(ValueError):
                checked_rows(root, 1, allow_errors=True)


if __name__ == "__main__":
    unittest.main()
