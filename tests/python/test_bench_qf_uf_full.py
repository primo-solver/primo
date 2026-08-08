import sys
import unittest
from pathlib import Path

from scripts.bench_interesting_qf_uf import run_solver as run_interesting_solver
from scripts.bench_qf_uf_full import run_solver as run_full_solver


class RunSolverClassificationTests(unittest.TestCase):
    def assert_runner_classification(self, runner) -> None:
        def run_python(source: str) -> dict[str, object]:
            return runner(
                [sys.executable, "-c", source], Path("unused.smt2"), 5.0
            )

        result = run_python("import sys; print('unsupported'); sys.exit(2)")

        self.assertEqual(result["answer"], "unsupported")
        self.assertEqual(result["status"], "unsupported")
        self.assertEqual(result["exit_code"], 2)

        result = run_python("print('sat')")

        self.assertEqual(result["answer"], "sat")
        self.assertEqual(result["status"], "ok")

        result = run_python("import sys; print('sat'); sys.exit(2)")

        self.assertEqual(result["answer"], "sat")
        self.assertEqual(result["status"], "error")

        result = run_python("print('diagnostic')")

        self.assertEqual(result["answer"], "error")
        self.assertEqual(result["status"], "error")

    def test_full_runner_classification(self) -> None:
        self.assert_runner_classification(run_full_solver)

    def test_interesting_runner_classification(self) -> None:
        self.assert_runner_classification(run_interesting_solver)


if __name__ == "__main__":
    unittest.main()
