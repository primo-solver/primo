import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "lexical_conformance",
    Path(__file__).resolve().parents[2] / "scripts/check_smtlib_lexical_conformance.py",
)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class ClassificationTests(unittest.TestCase):
    def test_answer_does_not_hide_error(self):
        case = {"valid": True, "answers": ["sat"]}
        self.assertEqual(
            AUDIT.classify(case, 0, '(error "bad")\nsat\n', "")[0], "valid-rejected"
        )

    def test_crash_is_not_rejection(self):
        self.assertEqual(AUDIT.classify({"valid": False}, -11, "", "")[0], "crash")

    def test_nonzero_is_not_automatically_rejection(self):
        self.assertEqual(
            AUDIT.classify({"valid": False}, 2, "", "")[0], "process-error"
        )

    def test_checker_is_not_answer_oracle(self):
        case = {"valid": True, "answers": ["unsat"]}
        self.assertEqual(
            AUDIT.classify(case, 0, "", "", checker=True)[0], "valid-accepted"
        )
        self.assertEqual(AUDIT.classify(case, 0, "sat\n", "")[0], "wrong-answer")

    def test_all_query_answers_required(self):
        case = {"valid": True, "answers": ["sat", "unsat"]}
        self.assertEqual(AUDIT.classify(case, 0, "sat\n", "")[0], "response-mismatch")


if __name__ == "__main__":
    unittest.main()
