import csv
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.bench_interesting_qf_uf import run_solver as run_interesting_solver
from scripts.bench_qf_uf_full import run_solver as run_full_solver
from scripts.bench_qf_uf_full import write_summary


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

    def test_summary_ignores_answer_printed_by_timed_out_process(self) -> None:
        rows = [
            {
                "status": "ok",
                "answer": "sat",
                "expected": "sat",
                "wrong": "false",
                "timed_out_with_stats": "false",
                "family": "sample",
                "seconds": "1.0",
            },
            {
                "status": "timeout",
                "answer": "sat",
                "expected": "",
                "wrong": "false",
                "timed_out_with_stats": "false",
                "family": "sample",
                "seconds": "5.0",
            },
        ]
        with tempfile.TemporaryDirectory() as temp:
            summary = Path(temp) / "summary.csv"
            write_summary(rows, summary, 5.0)
            with summary.open(newline="", encoding="utf-8") as handle:
                metrics = {
                    row["metric"]: row["value"] for row in csv.DictReader(handle)
                }

        self.assertEqual(metrics["solved"], "1")
        self.assertEqual(metrics["answer.sat"], "1")
        self.assertEqual(metrics["status.timeout"], "1")


if __name__ == "__main__":
    unittest.main()
