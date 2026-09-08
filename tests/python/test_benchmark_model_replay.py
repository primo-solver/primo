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
    def test_timing_manifest_is_sharded_once_and_preserves_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("a.smt2", "b.smt2", "c.smt2"):
                (root / name).write_text("(check-sat)")
            manifest = root / "selected.txt"
            manifest.write_text("c.smt2\na.smt2\nb.smt2\n")
            binary = root / "binary"
            binary.write_text("binary")
            identity = {
                "path": str(binary),
                "sha256": MODULE.digest(binary),
                "version": "git: abc",
            }
            out = root / "out"
            with (
                mock.patch.object(
                    sys,
                    "argv",
                    [
                        str(SCRIPT),
                        "benchmark",
                        "--root",
                        str(root),
                        "--manifest",
                        str(manifest),
                        "--shards",
                        "2",
                        "--shard-index",
                        "0",
                        "--out",
                        str(out),
                    ],
                ),
                mock.patch.object(MODULE, "executable_identity", return_value=identity),
                mock.patch.object(
                    MODULE.subprocess, "run", return_value=mock.Mock(returncode=0)
                ) as run,
            ):
                self.assertEqual(MODULE.main(), 0)
            metadata = json.loads((out / "provenance.json").read_text())
            self.assertEqual(set(metadata["inputs"]), {"c.smt2", "b.smt2"})
            command = run.call_args.args[0]
            self.assertEqual(command[command.index("--shards") + 1], "1")
            selected = (
                Path(command[command.index("--manifest") + 1]).read_text().splitlines()
            )
            self.assertEqual(selected, [str(root / "c.smt2"), str(root / "b.smt2")])

    def test_streaming_digest_supports_cluster_python(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "bytes"
            path.write_bytes(b"abc")
            self.assertEqual(
                MODULE.digest(path),
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            )

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
