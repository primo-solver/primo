from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts" / "summarize_component_pairs.py"
SPEC = importlib.util.spec_from_file_location("component_pairs", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ComponentPairsTests(unittest.TestCase):
    def test_three_pair_loss_threshold_and_missing_round(self) -> None:
        for lost_rounds in ((), (0,), (0, 2)):
            with self.subTest(lost_rounds=lost_rounds), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "node-0.txt").write_text("allocated-node\n")
                for rnd in range(3):
                    for arm in ("baseline", "candidate"):
                        shard = root / f"round-{rnd}" / arm / "shard-0"
                        shard.mkdir(parents=True)
                        (shard / "provenance.json").write_text(json.dumps({
                            "status": "complete", "inputs_unchanged": True,
                            "inputs": {"case.smt2": "hash"}, "solver_args": ["--quiet"],
                            "timeout": 60, "shards": 1, "shard_index": 0,
                            "solver": {"sha256": arm},
                        }))
                        outcome = ("timeout,unknown,-15,true,60" if arm == "candidate"
                                   and rnd in lost_rounds else "ok,unsat,0,false,1")
                        (shard / "answers.csv").write_text(
                            "file,family,status,answer,exit_code,timeout,seconds,wrong\n"
                            f"case.smt2,family,{outcome},false\n"
                        )
                    if rnd == 1:
                        with self.assertRaises(ValueError):
                            MODULE.summarize_loss_reruns(root, 1)
                report = MODULE.summarize_loss_reruns(root, 1)
                self.assertEqual(report["loss_counts"].get("case.smt2", 0), len(lost_rounds))
                self.assertEqual(report["loss_rerun_status"],
                                 "repeated-loss" if len(lost_rounds) >= 2 else "no-repeated-loss")
                for field, changed, diagnostic in (
                    ("inputs", {"case.smt2": "changed-hash"}, "input content"),
                    ("solver_args", ["--different-profile"], "solver options"),
                ):
                    originals = {}
                    for arm in ("baseline", "candidate"):
                        path = root / f"round-2/{arm}/shard-0/provenance.json"
                        originals[path] = path.read_text()
                        metadata = json.loads(originals[path])
                        metadata[field] = changed
                        path.write_text(json.dumps(metadata))
                    with self.assertRaisesRegex(ValueError, diagnostic):
                        MODULE.summarize_loss_reruns(root, 1)
                    for path, original in originals.items():
                        path.write_text(original)
                # A process error cannot be accepted as ordinary timing noise.
                target = root / "round-2/candidate/shard-0/answers.csv"
                target.write_text(target.read_text().replace("ok,unsat,0,false,1", "error,sat,3,false,1")
                                  .replace("timeout,unknown,-15,true,60", "error,sat,3,false,1"))
                self.assertEqual(MODULE.summarize_loss_reruns(root, 1)["loss_rerun_status"],
                                 "failed-correctness")

    def test_serial_batch_harnesses_request_one_node_and_task(self) -> None:
        for name in (
            "slurm_component_migration_compare.sbatch",
            "slurm_real_corpus_validation.sbatch",
            "slurm_component_loss_rerun.sbatch",
        ):
            with self.subTest(script=name):
                directives = (SCRIPT.parent / name).read_text().splitlines()
                self.assertIn("#SBATCH --nodes=1", directives)
                self.assertIn("#SBATCH --ntasks=1", directives)

    def test_lost_solve_job_runs_exactly_three_alternating_pairs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "scripts/benchmark_model_replay.py").write_text(
                "import json, sys\n"
                "with open('calls.jsonl', 'a') as stream:\n"
                "    stream.write(json.dumps(sys.argv[1:]) + '\\n')\n"
            )
            environment = {
                **os.environ,
                "PRIMO_WORKDIR": str(root),
                "PRIMO_COMPARE_OUT_ROOT": str(root / "out"),
                "PRIMO_COMPARE_ROOT": str(root / "corpus"),
                "PRIMO_COMPARE_MANIFEST": str(root / "manifest.txt"),
                "PRIMO_COMPARE_SHARDS": "1",
                "SLURM_ARRAY_TASK_ID": "0",
                "PRIMO_BASELINE_SOLVER": "baseline-primo",
                "PRIMO_CANDIDATE_SOLVER": "candidate-primo",
                "PRIMO_BASELINE_GIT": "baseline-git",
                "PRIMO_EXPECTED_GIT": "candidate-git",
                "PRIMO_COMPARE_TIMEOUT": "60",
                "PRIMO_SOLVER_ARGS": "--no-qf-uflia-model-combination",
            }
            subprocess.run(
                ["bash", str(SCRIPT.parent / "slurm_component_loss_rerun.sbatch")],
                env=environment, check=True, timeout=10,
            )
            calls = [json.loads(line) for line in (root / "calls.jsonl").read_text().splitlines()]
            self.assertEqual(len(calls), 6)
            for index, arm in enumerate(("baseline", "candidate", "candidate",
                                         "baseline", "baseline", "candidate")):
                call = calls[index]
                self.assertEqual(call[0], "benchmark")
                self.assertEqual(call[call.index("--solver") + 1], f"{arm}-primo")
                self.assertEqual(call[call.index("--expected-git") + 1], f"{arm}-git")
                self.assertEqual(call[call.index("--timeout") + 1], "60")
                self.assertEqual(call[call.index("--manifest") + 1], str(root / "manifest.txt"))
                self.assertIn("--solver-arg=--quiet", call)
                self.assertIn("--solver-arg=--stats", call)
                self.assertIn("--solver-arg=--no-qf-uflia-model-combination", call)
                self.assertEqual(call[call.index("--out") + 1],
                                 str(root / f"out/round-{index // 2}/{arm}/shard-0"))

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
