from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).parents[2] / "scripts" / "benchmark_model_replay.py"
SPEC = importlib.util.spec_from_file_location("benchmark_model_replay", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BenchmarkModelReplayTests(unittest.TestCase):
    def test_selection_requires_successful_process_not_known_status(self) -> None:
        good = {
            "file": "x",
            "status": "ok",
            "answer": "sat",
            "exit_code": "0",
            "timeout": "false",
            "expected": "",
        }
        rows = [
            good,
            {**good, "exit_code": "9"},
            {**good, "timeout": "true"},
            {**good, "status": "error"},
            {**good, "answer": "unsat"},
        ]
        self.assertEqual(MODULE.selected_sat_rows(rows), [good])

    def test_coverage_distinguishes_failure_incomplete_and_complete(self) -> None:
        good = {"file": "x", "outcome": "pass", "validator": "dolmen", "answer": "sat"}
        for rows, status in [
            ([good], "complete"),
            ([], "incomplete"),
            ([good, good], "failed"),
            ([{**good, "outcome": "answer-changed"}], "failed"),
            ([{**good, "outcome": "solver-timeout"}], "incomplete"),
            ([{**good, "validator": "z3-fallback"}], "failed"),
            ([{**good, "file": "unselected"}], "failed"),
        ]:
            with self.subTest(rows=rows):
                self.assertEqual(MODULE.coverage(["x"], rows)["status"], status)
        self.assertEqual(MODULE.coverage([], [])["status"], "complete")

    def test_replay_preserves_profile_checks_inputs_and_resumes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            case.write_text("(check-sat)")
            identity = {"path": "/solver", "sha256": "binary", "version": "v1"}
            metadata = {
                "status": "complete",
                "run_id": "parent",
                "root": str(root),
                "solver": identity,
                "solver_args": ["--quiet", "--preprocessing-mode=off"],
                "inputs": {"case.smt2": MODULE.digest(case)},
            }
            (root / "provenance.json").write_text(json.dumps(metadata))
            (root / "answers.csv").write_text(
                "file,status,answer,exit_code,timeout\ncase.smt2,ok,sat,0,false\n"
            )
            calls = []
            real_digest = MODULE.digest

            def validate(command, **kwargs):
                calls.append(command)
                output = Path(command[command.index("--out") + 1])
                output.mkdir(parents=True)
                (output / "summary.csv").write_text(
                    "file,answer,outcome,validator\n" + str(case) + ",sat,pass,dolmen\n"
                )
                return mock.Mock(returncode=0)

            argv = [
                str(SCRIPT),
                "replay",
                "--out",
                str(root),
                "--dolmen-arg=--strict=false",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(MODULE, "executable_identity", return_value=identity),
                mock.patch.object(
                    MODULE,
                    "digest",
                    side_effect=lambda path: (
                        "binary" if str(path) == "/solver" else real_digest(path)
                    ),
                ),
                mock.patch.object(MODULE.subprocess, "run", side_effect=validate),
            ):
                self.assertEqual(MODULE.main(), 0)
                self.assertEqual(MODULE.main(), 0)
                self.assertEqual(len(calls), 1)
                self.assertIn("--solver-arg=--preprocessing-mode=off", calls[0])
                self.assertIn("--solver-arg=--check-model", calls[0])
                self.assertIn("--expect-sat", calls[0])
                case.write_text("(assert false)(check-sat)")
                self.assertEqual(MODULE.main(), 1)


if __name__ == "__main__":
    unittest.main()
