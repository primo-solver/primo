from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts" / "summarize_component_pairs.py"
SPEC = importlib.util.spec_from_file_location("component_pairs", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ComponentPairsTests(unittest.TestCase):
    def test_serial_batch_harnesses_request_one_node_and_task(self) -> None:
        for name in (
            "slurm_component_migration_compare.sbatch",
            "slurm_real_corpus_validation.sbatch",
        ):
            with self.subTest(script=name):
                directives = (SCRIPT.parent / name).read_text().splitlines()
                self.assertIn("#SBATCH --nodes=1", directives)
                self.assertIn("#SBATCH --ntasks=1", directives)

    def test_complete_pair_provenance_and_mismatch_rejection(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "node-0.txt").write_text("allocated-node\n")
            for round_number in (0, 1):
                for arm in ("baseline", "candidate"):
                    shard = root / f"round-{round_number}" / arm / "shard-0"
                    shard.mkdir(parents=True)
                    metadata = {
                        "status": "complete",
                        "inputs_unchanged": True,
                        "inputs": {"case.smt2": "input-hash"},
                        "solver_args": ["--quiet"],
                        "timeout": 60,
                        "shards": 1,
                        "shard_index": 0,
                        "solver": {"sha256": arm},
                    }
                    (shard / "provenance.json").write_text(json.dumps(metadata))
                    (shard / "answers.csv").write_text(
                        "file,family,status,answer,exit_code,timeout,seconds,wrong\n"
                        "case.smt2,family,ok,sat,0,false,1,false\n"
                    )
            report = MODULE.summarize(root, 1, 1.01)
            self.assertEqual(report["timing_status"], "pass")
            self.assertEqual(report["aggregate"]["candidate"]["solved"], 2)
            self.assertEqual(report["families"]["family"]["par2_ratio"], 1)
            target = root / "round-1/candidate/shard-0/provenance.json"
            metadata = json.loads(target.read_text())
            metadata["solver_args"] = ["--different-profile"]
            target.write_text(json.dumps(metadata))
            with self.assertRaisesRegex(ValueError, "provenance mismatch"):
                MODULE.summarize(root, 1, 1.01)

    def test_failed_and_unknown_processes_are_not_solved(self) -> None:
        good = {
            "status": "ok",
            "answer": "sat",
            "exit_code": "0",
            "timeout": "false",
            "seconds": "2",
        }
        for changed in (
            {"answer": "unknown"},
            {"exit_code": "3"},
            {"status": "error"},
            {"timeout": "true"},
        ):
            with self.subTest(changed=changed):
                bad = {**good, **changed}
                report = MODULE.compare({"case": good}, {"case": bad}, 60)
                self.assertEqual(report["candidate"]["solved"], 0)
                self.assertEqual(report["candidate"]["par2"], 120)
                self.assertEqual(report["lost"], ["case"])

    def test_disagreement_and_common_runtime_are_separate(self) -> None:
        good = {
            "status": "ok",
            "answer": "sat",
            "exit_code": "0",
            "timeout": "false",
            "seconds": "2",
        }
        report = MODULE.compare(
            {"case": good}, {"case": {**good, "answer": "unsat", "seconds": "1"}}, 60
        )
        self.assertEqual(report["disagreements"], ["case"])
        self.assertEqual(report["common_runtime_ratio"], 0.5)
        with self.assertRaises(ValueError):
            MODULE.compare({"case": good}, {}, 60)
