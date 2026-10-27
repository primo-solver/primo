import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/analyze_lra_bound_probe.py"
SPEC = importlib.util.spec_from_file_location("bound_probe", SCRIPT)
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class ProbeTests(unittest.TestCase):
    def test_numerical_cap_does_not_mark_causal_trace_as_prefix(self):
        events = [
            {"event": "begin", "schema": 2, "snapshot_limit": 1},
            {"event": "assignment", "lit": 1, "conflict": False},
            {"event": "propagate", "seq": 3, "lit": 2, "kind": "row", "reasons": [-1]},
            {"event": "assignment", "lit": 2, "conflict": False},
            {"event": "conflict", "seq": 4, "reasons": [-1, -2]},
            {"event": "end", "limit_reached": False, "snapshot_limit_reached": True},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            path.write_text("".join(json.dumps(e) + "\n" for e in events))
            result = PROBE.analyze(path)
        self.assertEqual(result["coverage"], "complete")
        self.assertEqual(result["numerical_coverage"], "prefix")
        self.assertEqual(
            result["event_metrics"]["conflict"]["shared_ancestry_events"], 1
        )
        self.assertEqual(result["event_metrics"]["conflict"]["expanded_smaller"], 1)

    def test_exact_strict_negative_reverse_interval(self):
        # s = -2*x + y, s <= 0 and y > 1 imply x > 1/2.
        bound = lambda value, reason: {"value": value, "reason": reason}
        event = {
            "upper": False,
            "row": 2,
            "target": 0,
            "basic_bound": bound(["0", "0"], -1),
            "terms": [
                {"var": 0, "coeff": "-2", "lower": None, "upper": None},
                {"var": 1, "coeff": "1", "lower": bound(["1", "1"], -2), "upper": None},
            ],
            "value": ["1/2", "1/2"],
            "reasons": [-1, -2],
        }
        PROBE.check_derived(event)
        event["value"] = ["1/2", "0"]
        with self.assertRaises(AssertionError):
            PROBE.check_derived(event)

    def test_later_stronger_snapshot_is_not_hidden_by_deduplication(self):
        event = {
            "event": "derived",
            "row": 1,
            "target": 1,
            "upper": True,
            "basic_bound": None,
            "terms": [
                {
                    "var": 0,
                    "coeff": "1",
                    "lower": None,
                    "upper": {"value": ["2", "0"], "reason": -1},
                }
            ],
            "value": ["2", "0"],
            "reasons": [-1],
            "installed": {"value": ["1", "0"], "reason": -2},
        }
        later = {**event, "installed": {"value": ["10", "0"], "reason": -3}}
        events = [
            {"event": "begin"},
            event,
            later,
            {"event": "end", "limit_reached": False},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            path.write_text("".join(json.dumps(e) + "\n" for e in events))
            result = PROBE.analyze(path)
        self.assertEqual(result["counts"]["distinct_derived"], 1)
        self.assertEqual(result["counts"]["derived_stronger_than_installed"], 1)
        self.assertEqual(result["counts"]["stronger_without_exact_atom"], 1)

    def test_shared_theory_parent_and_backtrack_instances(self):
        events = [
            {"event": "begin"},
            {"event": "assignment", "lit": 1, "conflict": False},
            {"event": "propagate", "seq": 1, "lit": 2, "kind": "row", "reasons": [-1]},
            {"event": "assignment", "lit": 2, "conflict": False},
            {"event": "propagate", "seq": 2, "lit": 3, "kind": "row", "reasons": [-1]},
            {"event": "assignment", "lit": 3, "conflict": False},
            {"event": "conflict", "seq": 3, "reasons": [-2, -3]},
            {"event": "backtrack", "removed": [2, 3]},
            {"event": "assignment", "lit": 2, "conflict": False},
            {"event": "conflict", "seq": 4, "reasons": [-1, -2]},
            {"event": "conflict", "seq": 5, "reasons": [-1, -1]},
            {"event": "end", "limit_reached": False},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            path.write_text("".join(json.dumps(e) + "\n" for e in events))
            result = PROBE.analyze(path)
        self.assertEqual(result["counts"]["reasons_with_shared_leaves"], 2)
        self.assertEqual(result["counts"]["theory_expansions_with_shared_leaves"], 1)
        self.assertEqual(result["shared_examples"][0]["leaves"], {1: 2})
        self.assertEqual(result["shared_examples"][0]["depth"], 2)
        self.assertEqual(result["coverage"], "complete")
