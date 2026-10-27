"""Scoring regressions use mocked processes; no solver batch runs locally."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


IRACE = load("scripts/irace/target_runner.py")
SMAC = load("scripts/smac/qfuf_gap_tune.py")


class TuningProcessStatusTests(unittest.TestCase):
    def test_exit_and_timeout_override_answers_in_both_wrappers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.smt2"
            path.write_text("(set-info :status sat)\n")
            log = Path(directory) / "irace.log"
            for answer in ("sat", "unsat"):
                for exit_code in (0, 1, -11, 124):
                    with self.subTest(answer=answer, exit_code=exit_code):
                        stdout, stderr = answer + "\n", "diagnostic\n"
                        timeout = exit_code == 124
                        result = subprocess.CompletedProcess([], exit_code, stdout, stderr)
                        kwargs = (
                            {"side_effect": subprocess.TimeoutExpired([], 5, stdout, stderr)}
                            if timeout else {"return_value": result}
                        )
                        expected_cost = 10 if exit_code else (5000 if answer == "unsat" else 1)
                        expected_status = "timeout" if timeout else (
                            "error" if exit_code else ("wrong" if answer == "unsat" else "ok")
                        )
                        with mock.patch.object(SMAC.subprocess, "run", **kwargs), mock.patch.object(
                            SMAC.time, "perf_counter", side_effect=[0, 1]
                        ):
                            row = SMAC.run_solver(["primo"], {}, path, 5)
                        self.assertEqual(float(row["cost"]), expected_cost)
                        self.assertEqual(row["status"], expected_status)
                        self.assertEqual(row["wrong"], str(expected_status == "wrong").lower())
                        self.assertEqual(row["exit_code"], exit_code)
                        self.assertEqual((row["stdout"], row["stderr"]), (stdout, stderr))
                        output = io.StringIO()
                        with mock.patch.object(IRACE.subprocess, "run", **kwargs), mock.patch.object(
                            IRACE.time, "perf_counter", side_effect=[0, 1]
                        ), mock.patch.object(IRACE.sys, "argv", ["runner", str(path)]), mock.patch.dict(
                            IRACE.os.environ,
                            {"PRIMO_TUNE_TIMEOUT": "5", "PRIMO_TUNE_LOG": str(log)}, clear=True
                        ), contextlib.redirect_stdout(output):
                            self.assertEqual(IRACE.main(), 0)
                        self.assertEqual(float(output.getvalue()), expected_cost)
                        lines = log.read_text().splitlines()
                        self.assertEqual(int(lines[-2].split(",")[5]), exit_code)
                        self.assertEqual(json.loads(lines[-1].removeprefix("# output: ")),
                                         {"stdout": stdout, "stderr": stderr})

    def test_smac_validation_csv_retains_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "input.smt2"
            path.write_text("(set-info :status sat)\n")
            with mock.patch.object(SMAC.subprocess, "run", return_value=
                                   subprocess.CompletedProcess([], 1, "sat\n", "crash\n")):
                SMAC.validate_rows("test", ["primo"], {}, [path], 5, root)
            self.assertIn("stdout,stderr", (root / "validation-runs.csv").read_text())


if __name__ == "__main__":
    unittest.main()
