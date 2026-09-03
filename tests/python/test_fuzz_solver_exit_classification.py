from __future__ import annotations

import csv
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts import (
    check_solver_seed_corpus,
    compare_with_z3,
    define_fun_macro_fuzz,
    finite_domain_fuzz,
    fuzz_external,
    qf_lra_boolean_arithmetic_fuzz,
    qf_uf_boolean_fuzz,
    qf_uflia_fuzz,
    qf_uflra_smoke_fuzz,
)


def post_answer_crash_command() -> list[str]:
    return [sys.executable, "-c", "import sys; print('sat'); sys.exit(9)"]


class FuzzSolverExitClassificationTests(unittest.TestCase):
    @staticmethod
    def make_solver(path: Path, exit_code: int) -> Path:
        path.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n"
            "print('sat')\n"
            f"sys.exit({exit_code})\n",
            encoding="utf-8",
        )
        path.chmod(0o755)
        return path

    def test_tuple_runners_reject_decisive_answer_before_crash(self) -> None:
        command = post_answer_crash_command()
        runners = (
            define_fun_macro_fuzz.run_solver,
            qf_uf_boolean_fuzz.run_solver,
            qf_uflia_fuzz.run_solver,
            qf_uflra_smoke_fuzz.run_solver,
        )
        for runner in runners:
            with self.subTest(runner=runner.__module__):
                self.assertEqual(runner(command, 2.0), ("CRASH", 9))

        answer, code, stdout, _stderr = compare_with_z3.run_solver(command, 2.0)
        self.assertEqual((answer, code), ("CRASH", 9))
        self.assertEqual(stdout.strip(), "sat")

    def test_path_runners_reject_decisive_answer_before_crash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary) / "case.smt2"
            case.write_text("(check-sat)\n", encoding="utf-8")
            self.assertEqual(
                fuzz_external.run_solver(post_answer_crash_command(), case, 2.0),
                ("CRASH", 9),
            )
            self.assertEqual(
                check_solver_seed_corpus.run(post_answer_crash_command(), case, 2.0),
                ("crash", 9),
            )

    def test_structured_runners_reject_decisive_answer_before_crash(self) -> None:
        finite = finite_domain_fuzz.run_solver(
            post_answer_crash_command(), 2.0, collect_stats=False
        )
        self.assertEqual((finite.answer, finite.returncode), ("CRASH", 9))

        boolean_arithmetic = qf_lra_boolean_arithmetic_fuzz.run_solver(
            post_answer_crash_command(), 2.0, collect_stats=False
        )
        self.assertEqual(
            (boolean_arithmetic.answer, boolean_arithmetic.returncode),
            ("CRASH", 9),
        )

    def test_unusable_reference_blocks_qf_uflia_campaign(self) -> None:
        classifiers = (
            lambda: qf_uflia_fuzz.classify("sat", ["sat", "CRASH"]),
            lambda: fuzz_external.classify("sat", ["sat", "CRASH"]),
            lambda: compare_with_z3.classify("sat", ["sat", "CRASH"]),
            lambda: define_fun_macro_fuzz.classify("sat", "CRASH"),
            lambda: qf_uf_boolean_fuzz.classify("sat", "CRASH"),
            lambda: qf_uflra_smoke_fuzz.classify("sat", "CRASH"),
        )
        for classifier in classifiers:
            with self.subTest(classifier=classifier):
                self.assertEqual(classifier(), "REFERENCE_UNUSABLE")

    def test_qf_uflia_cli_preserves_candidate_and_reference_crashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            good = self.make_solver(root / "good", 0)
            crashing = self.make_solver(root / "crashing", 9)
            scenarios = (
                (crashing, good, "CRASH"),
                (good, crashing, "REFERENCE_UNUSABLE"),
            )
            for index, (solver, reference, expected) in enumerate(scenarios):
                with self.subTest(expected=expected):
                    out = root / f"out-{index}"
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(Path(qf_uflia_fuzz.__file__)),
                            "--solver",
                            str(solver),
                            "--ref",
                            str(reference),
                            "--count",
                            "1",
                            "--out",
                            str(out),
                        ],
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(
                        result.returncode, 1, result.stdout + result.stderr
                    )
                    with (out / "summary.csv").open(
                        newline="", encoding="utf-8"
                    ) as summary:
                        rows = list(csv.DictReader(summary))
                    self.assertEqual(rows[0]["outcome"], expected)
                    self.assertEqual(len(list((out / "bugs").glob("*.smt2"))), 1)


if __name__ == "__main__":
    unittest.main()
