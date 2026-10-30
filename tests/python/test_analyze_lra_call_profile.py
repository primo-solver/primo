import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/analyze_lra_call_profile.py"
SPEC = importlib.util.spec_from_file_location("call_profile", SCRIPT)
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)


class CallProfileTests(unittest.TestCase):
    def analyze(self, rows, tail="", status="ok"):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "calls.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in rows) + tail)
            return PROFILE.analyze(path, status)

    def test_joint_metrics_include_zero_pivots_and_match_terminal_counts(self):
        rows = [
            {"event": "begin", "schema": 1},
            {"event": "checker", "checker": 1, "variables": 3},
        ]
        for index, (pivots, ns, conflict) in enumerate(
            [(0, 10, False), (3, 1000, True), (3, 500, False)], 1
        ):
            rows.append(
                {
                    "event": "check",
                    "checker": 1,
                    "call": index,
                    "pivots": pivots,
                    "nanoseconds": ns,
                    "conflict": conflict,
                    "export": "",
                }
            )
        rows.append({"event": "end", "calls": 3, "checkers": 1, "exported": 0})
        result = self.analyze(rows)
        self.assertEqual(result["coverage"], "complete")
        self.assertEqual(result["total_pivots"], 6)
        self.assertEqual(result["zero_pivot_calls"], 1)
        self.assertEqual(result["pivot_bins"]["2-3"]["p50_ns"], 500)
        self.assertEqual(result["pivot_bins"]["2-3"]["p99_ns"], 1000)
        self.assertEqual(self.analyze(rows, status="timeout")["coverage"], "prefix")
        rows[-1]["calls"] = 4
        with self.assertRaisesRegex(ValueError, "terminal counts"):
            self.analyze(rows)

    def test_truncated_tail_and_missing_end_are_censored(self):
        rows = [{"event": "begin", "schema": 1}, {"event": "checker", "checker": 1}]
        result = self.analyze(rows, '{"event":"check"')
        self.assertEqual(result["coverage"], "prefix")
        self.assertTrue(result["partial_line"])
        with self.assertRaisesRegex(ValueError, "malformed complete"):
            self.analyze(rows, '{"event":\n')

    def test_empty_trace_with_truncated_tail_stays_censored(self):
        rows = [
            {"event": "begin", "schema": 1},
            {"event": "end", "checkers": 0, "calls": 0, "exported": 0},
        ]
        self.assertEqual(self.analyze(rows, '{"event":')["coverage"], "prefix")

    def test_capture_completion_and_abandoned_end(self):
        rows = [
            {"event": "begin", "schema": 2},
            {"event": "checker", "checker": 1},
            {"event": "capture", "checker": 1, "call": 1, "file": "capture.smt2"},
        ]
        result = self.analyze(rows, status="timeout")
        self.assertIsNone(result["captures"][0]["completion"])
        self.assertEqual(result["coverage"], "prefix")
        end = {"event": "end", "calls": 0, "checkers": 1, "exported": 0}
        self.assertEqual(self.analyze([*rows, end])["coverage"], "prefix")
        rows.append(
            {
                "event": "check",
                "checker": 1,
                "call": 1,
                "pivots": 5,
                "nanoseconds": 1000,
                "conflict": True,
                "export": "",
            }
        )
        end["calls"] = 1
        result = self.analyze([*rows, end])
        self.assertEqual(result["coverage"], "complete")
        self.assertTrue(result["captures"][0]["completion"]["conflict"])
        with self.assertRaisesRegex(ValueError, "duplicate capture"):
            self.analyze([*rows[:3], rows[2]])

    def test_nonconsecutive_calls_rejected(self):
        rows = [
            {"event": "begin", "schema": 1},
            {"event": "checker", "checker": 1},
            {"event": "check", "checker": 1, "call": 2},
        ]
        with self.assertRaisesRegex(ValueError, "call sequence"):
            self.analyze(rows)


if __name__ == "__main__":
    unittest.main()
