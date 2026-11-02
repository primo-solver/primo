from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from classify_soi_mutations import classify, run


class MutationClassificationTests(unittest.TestCase):
    def test_timeout_preserves_reference_rejection_diagnostics(self):
        failure = subprocess.TimeoutExpired(
            ["reference"], 5, output=b"nonlinear input\n", stderr=b"diagnostic\n"
        )
        with patch("classify_soi_mutations.subprocess.run", side_effect=failure):
            result = run(["reference"], Path("unused"), 5)
        self.assertTrue(result["timeout"])
        self.assertEqual(result["stdout"], "nonlinear input\n")
        self.assertEqual(result["stderr"], "diagnostic\n")

    def test_reference_must_succeed_and_valid_native_errors_block(self):
        sat = {"exit": 0, "stdout": "sat\n", "stderr": ""}
        unsat = {"exit": 0, "stdout": "unsat\n", "stderr": ""}
        error = {"exit": 2, "stdout": '(error "unsupported")\n', "stderr": ""}
        self.assertEqual(classify(sat, sat), "PASS")
        self.assertEqual(classify(sat, unsat), "WRONG_ANSWER")
        self.assertEqual(classify(error, sat), "VALID_INPUT_NATIVE_ERROR")
        invalid = {
            "exit": 1,
            "stdout": '(error "logic does not support nonlinear arithmetic")\nunsat\n',
        }
        self.assertEqual(classify(error, invalid), "INVALID_GENERATED_NONLINEAR")
        self.assertEqual(
            classify(error, {"exit": 1, "stdout": "unsat\n"}),
            "UNTRIAGED_REFERENCE_ERROR",
        )
        self.assertEqual(classify({"exit": -11}, invalid), "CRASH")
        self.assertEqual(
            classify(error, {"timeout": True}), "UNTRIAGED_NATIVE_REJECTION"
        )
        self.assertEqual(
            classify(error, {"timeout": True, "stdout": invalid["stdout"]}),
            "INVALID_GENERATED_NONLINEAR",
        )
