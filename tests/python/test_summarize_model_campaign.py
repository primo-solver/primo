from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts" / "summarize_model_campaign.py"
SPEC = importlib.util.spec_from_file_location("summarize_model_campaign", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ModelCampaignSummaryTests(unittest.TestCase):
    def test_unknown_and_failed_process_are_not_solved(self) -> None:
        row = {"status": "ok", "answer": "sat", "exit_code": "0", "timeout": "false",
               "seconds": "2", "wrong": "false"}
        result = MODULE.summarize([row, {**row, "answer": "unknown"},
                                   {**row, "exit_code": "9"}], 60)
        self.assertEqual(result["solved"], 1)
        self.assertEqual(result["par2"], 242)

    def test_missing_shard_or_model_is_not_complete(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shard = root / "shard-0"
            shard.mkdir()
            provenance = {"status": "complete", "shard_index": 0, "shards": 2,
                          "inputs": {"x.smt2": "hash"}, "root": str(root)}
            (shard / "provenance.json").write_text(json.dumps(provenance))
            (shard / "answers.csv").write_text(
                "file,family,status,answer,exit_code,timeout,seconds,wrong\n"
                "x.smt2,family,ok,sat,0,false,2,false\n")
            summary, _ = MODULE.load_run(root, 2, 60)
            self.assertFalse(summary["timing_complete"])
            self.assertFalse(summary["models"]["fully_validated"])
            self.assertEqual(summary["models"]["missing"], 1)
            provenance["shards"] = 1
            (shard / "provenance.json").write_text(json.dumps(provenance))
            summary, _ = MODULE.load_run(root, 1, 60)
            self.assertTrue(summary["timing_complete"])
            self.assertFalse(summary["models"]["fully_validated"])
            models = shard / "model-validation" / "models"
            models.mkdir(parents=True)
            (models.parent / "coverage.json").write_text('{"status":"complete"}')
            (models / "summary.csv").write_text(
                f"file,answer,outcome,validator\n{root}/x.smt2,sat,pass,dolmen\n")
            summary, _ = MODULE.load_run(root, 1, 60)
            self.assertTrue(summary["models"]["fully_validated"])

    def test_journal_progress_does_not_complete_a_shard(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shard = root / "shard-0"
            shard.mkdir()
            (shard / "provenance.json").write_text(json.dumps({
                "status": "running", "shard_index": 0, "shards": 1,
                "inputs": {"x": "hash"}, "root": str(root)}))
            row = {"file": "x", "family": "f", "status": "ok", "answer": "unsat",
                   "exit_code": 0, "timeout": "false", "seconds": "2"}
            (shard / "answers.csv.jsonl").write_text(json.dumps(row) + '\n{"partial":')
            summary, _ = MODULE.load_run(root, 1, 60)
            self.assertEqual(summary["solved"], 1)
            self.assertFalse(summary["timing_complete"])


if __name__ == "__main__":
    unittest.main()
