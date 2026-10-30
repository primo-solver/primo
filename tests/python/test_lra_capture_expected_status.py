import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

with patch.object(
    sys, "path", [str(Path(__file__).resolve().parents[2] / "scripts"), *sys.path]
):
    from run_lra_call_distribution import expected_status


class CaptureExpectedStatusTests(unittest.TestCase):
    def test_independent_label_survives_unknown_capture_annotation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "capture.smt2"
            path.write_text("(set-info :status unknown)\n(check-sat)\n")
            self.assertEqual(
                expected_status(path, root, {"capture.smt2": "sat"}), "sat"
            )
            self.assertEqual(expected_status(path, root, {}), "")

    def test_conflicting_labels_fail_and_unknown_does_not_erase_known(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "capture.smt2"
            path.write_text("(set-info :status sat)\n(check-sat)\n")
            self.assertEqual(
                expected_status(path, root, {"capture.smt2": "unknown"}), "sat"
            )
            with self.assertRaisesRegex(ValueError, "contradictory"):
                expected_status(path, root, {"capture.smt2": "unsat"})
