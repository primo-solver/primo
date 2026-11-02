from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "check_soi_explanations",
    Path(__file__).resolve().parents[2] / "scripts/check_soi_explanations.py",
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RefutationProtocolTests(unittest.TestCase):
    def test_answer_does_not_hide_reference_crash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = root / "reference"
            reference.write_text("#!/bin/sh\necho unsat\nexit 1\n")
            reference.chmod(0o755)
            problem = root / "fixture.smt2"
            problem.write_text("(check-sat)\n")
            self.assertEqual(module.refute(reference, problem, 2)["status"], "failure")

    def test_sat_is_not_a_valid_explanation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = root / "reference"
            reference.write_text("#!/bin/sh\necho sat\n")
            reference.chmod(0o755)
            problem = root / "fixture.smt2"
            problem.write_text("(check-sat)\n")
            self.assertEqual(module.refute(reference, problem, 2)["status"], "failure")
