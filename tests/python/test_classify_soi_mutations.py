from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from classify_soi_mutations import classify


class MutationClassificationTests(unittest.TestCase):
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
