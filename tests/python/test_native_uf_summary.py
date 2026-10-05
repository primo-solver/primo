import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from summarize_native_uf import LABELS, summarize


class NativeUfSummaryTests(unittest.TestCase):
    def test_lra_family_uses_complete_path_components(self):
        row = {"index": 0, "file": "incremental/QF_LRA/hybrid_networks/a.smt2",
               "queries": 1, "answers": ["sat"], "outcome": "complete",
               "seconds": 1, "par2": 1}
        other = dict(row, index=1, file="incremental/QF_LRA/other_hybrid_networks/b.smt2")
        data = {label: {0: copy.deepcopy(row), 1: copy.deepcopy(other)} for label in LABELS}
        models = {"assumptions": {0: {"outcome": "pass", "queries": [{"answer": "sat"}]}}}
        summary = summarize(data, models, 2, ["hybrid_networks"])
        families = summary["profiles"]["assumptions"]["families"]
        self.assertEqual(set(families), {"hybrid_networks"})
        self.assertEqual(families["hybrid_networks"]["sessions"], 1)
        self.assertEqual(families["hybrid_networks"]["validation"]["sat_models"], 1)

    def test_timeout_partial_answers_do_not_count_as_completed(self):
        row = {"index": 0, "file": "2018-Goel-hwbench/example.smt2", "queries": 1,
               "answers": ["sat"], "outcome": "complete", "seconds": 2, "par2": 2,
               "maxrss_kib": 1024}
        data = {label: {0: copy.deepcopy(row)} for label in LABELS}
        data["assumptions"][0].update(outcome="timeout", seconds=30, par2=60)
        summary = summarize(data, {}, 2)
        assumptions = summary["profiles"]["assumptions"]
        self.assertEqual(assumptions["completed_queries"], 0)
        self.assertEqual(assumptions["partial_queries"], 1)
        self.assertEqual(assumptions["mean_par2"], 60)
        self.assertFalse(assumptions["full_archive_timing"])
        self.assertEqual(summary["assumptions_vs_rebuild"]["losses"], [0])
        self.assertEqual(summary["assumptions_vs_rebuild"]["both_complete"], 0)

    def test_disagreement_and_changed_replay_remain_unqualified(self):
        row = {"index": 0, "file": "example.smt2", "queries": 1, "answers": ["sat"],
               "outcome": "complete", "seconds": 1, "par2": 1}
        data = {label: {0: copy.deepcopy(row)} for label in LABELS}
        data["z3"][0]["answers"] = ["unsat"]
        models = {"assumptions": {0: {"outcome": "pass", "queries": [{"answer": "unsat"}]}}}
        summary = summarize(data, models, 1)
        self.assertEqual(len(summary["reference_disagreements"]), 3)
        validation = summary["profiles"]["assumptions"]["validation"]
        self.assertEqual(validation["qualified_complete_sessions"], 0)
        self.assertEqual(validation["changed_answers"], [0])
        self.assertEqual(validation["complete_without_qualification"], [0])


if __name__ == "__main__":
    unittest.main()
