"""Only successful SAT queries may be sent to a get-model replay."""

import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts/select_fuzz_sat_inputs.py"
SPEC = importlib.util.spec_from_file_location("select_fuzz_sat_inputs", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SatSelectionTests(unittest.TestCase):
    def test_unsat_timeout_and_post_answer_error_are_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory) / "case.smt2"
            case.write_text("(check-sat)\n")
            row = {"case": str(case), "ours": "sat", "ours_code": "0", "outcome": "PASS"}
            excluded = [row | change for change in [
                {"ours": "unsat"}, {"ours_code": "2"}, {"outcome": "TIMEOUT"},
                {"ours": "unknown"}, {"outcome": "CRASH"},
            ]]
            self.assertEqual(MODULE.select([row, *excluded]), [str(case)])
            with self.assertRaises(ValueError):
                MODULE.select([row, row])

    def test_missing_successful_input_is_an_error(self):
        with self.assertRaises(ValueError):
            MODULE.select([{"case": "/nonexistent/required-input.smt2", "ours": "sat", "outcome": "PASS"}])


if __name__ == "__main__":
    unittest.main()
