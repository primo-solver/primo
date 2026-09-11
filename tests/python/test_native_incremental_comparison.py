import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from compare_native_incremental import classify_execution, run_session


class NativeComparisonTests(unittest.TestCase):
    def test_real_subprocess_post_answer_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            source = out / "input.smt2"
            source.write_text("(check-sat)\n")
            command = [sys.executable, "-c", "print('sat', flush=True); raise SystemExit(3)"]
            code, timed_out, _ = run_session(command, source, out, 5)
            result = classify_execution(code, timed_out, (out / "stdout").read_text(), ["sat"])
            self.assertEqual(result["outcome"], "process-error")
            self.assertEqual(code, 3)

    def test_real_subprocess_timeout_is_not_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            source = out / "input.smt2"
            source.write_text("(check-sat)\n")
            command = [sys.executable, "-c", "import time; print('sat', flush=True); time.sleep(5)"]
            code, timed_out, _ = run_session(command, source, out, 0.1)
            result = classify_execution(code, timed_out, (out / "stdout").read_text(), ["sat"])
            self.assertEqual(result["outcome"], "timeout")
            self.assertIsNone(code)


if __name__ == "__main__":
    unittest.main()
