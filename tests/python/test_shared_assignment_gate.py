"""Qualification must not accept an error hidden behind another reference timeout."""

import importlib.util
import sys
import unittest
from pathlib import Path
from unittest import mock

SCRIPTS = Path(__file__).parents[2] / "scripts"
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location("shared_assignment_gate", SCRIPTS / "shared_assignment_gate.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class AssignmentGateTests(unittest.TestCase):
    def test_reference_timeout_does_not_hide_failure(self):
        passes = [{"ours": "sat", "refs": "sat sat", "outcome": "PASS"}] * 999
        for row, blocked in [
            ({"ours": "sat", "refs": "sat TIMEOUT", "outcome": "REFERENCE_UNUSABLE"}, False),
            ({"ours": "unsat", "refs": "sat TIMEOUT", "outcome": "REFERENCE_UNUSABLE"}, True),
            ({"ours": "sat", "refs": "sat CRASH", "outcome": "REFERENCE_UNUSABLE"}, True),
            ({"ours": "TIMEOUT", "refs": "sat unsat", "outcome": "TIMEOUT"}, True),
            ({"ours": "CRASH", "refs": "sat sat", "outcome": "CRASH"}, True),
        ]:
            with self.subTest(row=row):
                self.assertEqual(MODULE.fuzz_summary(passes + [row], 1)["hard_failure"], blocked)

    def test_refuses_execution_without_slurm(self):
        with mock.patch.dict(MODULE.os.environ, {}, clear=True), mock.patch.object(
            sys, "argv", ["gate", "--task", "0", "--expected-git", "abc", "--out", "unused",
                          "--uf-seeds", "unused"]
        ), mock.patch.object(MODULE.subprocess, "check_output") as launch:
            with self.assertRaises(SystemExit) as stopped:
                MODULE.main()
            self.assertEqual(stopped.exception.code, 2)
            launch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
