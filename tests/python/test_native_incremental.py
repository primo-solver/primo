import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from check_native_incremental import classify_execution, declared_answers, digest


class NativeIncrementalTests(unittest.TestCase):
    def test_unknown_status_is_not_an_answer_oracle(self):
        result = classify_execution(0, False, "sat\nunsat\n", ["unknown", "unsat"])
        self.assertEqual(result["outcome"], "complete-with-unverified-answers")
        self.assertEqual(result["queries_without_status_oracle"], 1)
        self.assertEqual(result["mismatches"], [])

    def test_exit_and_timeout_override_answers(self):
        self.assertEqual(
            classify_execution(1, False, "sat\n", ["sat"])["outcome"], "process-error"
        )
        self.assertEqual(
            classify_execution(0, True, "sat\n", ["sat"])["outcome"], "timeout"
        )
        self.assertEqual(
            classify_execution(1, False, "sat\n", ["unsat"])["outcome"], "process-error"
        )
        self.assertEqual(
            classify_execution(0, False, "sat\n", ["unsat"])["outcome"], "wrong-answer"
        )
        self.assertEqual(
            classify_execution(0, False, "sat\n", ["sat"])["outcome"], "pass"
        )
        self.assertEqual(
            classify_execution(0, False, "unknown\n", ["sat"])["outcome"], "unknown"
        )

    def test_query_offsets_require_adjacent_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "input.smt2"
            first = b"(set-info :status unsat)\n(check-sat)"
            full = first + b"(pop 1)(set-info :status sat)\n(check-sat)"
            path.write_bytes(full)
            self.assertEqual(
                declared_answers(path, [len(first), len(full)]), ["unsat", "sat"]
            )
            self.assertEqual(len(digest(path)), 64)
            for offsets in [[len(first) - 1], [len(full), len(first)]]:
                with self.assertRaises(ValueError):
                    declared_answers(path, offsets)


if __name__ == "__main__":
    unittest.main()
