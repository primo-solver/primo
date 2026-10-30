import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

with patch.object(
    sys, "path", [str(Path(__file__).resolve().parents[2] / "scripts"), *sys.path]
):
    from qualify_lra_case_study import decisive, model_checks
    from validate_qfuf_models_with_dolmen import model_problem_text


class CaseStudyQualificationTests(unittest.TestCase):
    def test_timeout_answer_is_not_decisive_and_nonzero_exit_cannot_pass(self):
        self.assertIsNone(
            decisive({"status": "timeout", "answer": "sat", "exit_code": 0})
        )
        with self.assertRaises(AssertionError):
            decisive({"status": "ok", "answer": "sat", "exit_code": -11})

    def test_model_join_rejects_missing_changed_and_failed_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            corpus = root / "corpus"
            (corpus / "cubes").mkdir(parents=True)
            source = corpus / "cubes/a.smt2"
            source.write_text(
                "(set-logic QF_LRA)\n(set-info :status sat)\n(check-sat)\n"
            )
            models = root / "models"
            models.mkdir()
            with self.assertRaises(AssertionError):
                model_checks(models, corpus, {"cubes/a.smt2"})
            (models / "metadata.json").write_text(
                json.dumps(
                    {
                        "status": "complete",
                        "expect_sat": True,
                        "require_dolmen_pass": True,
                        "solver_args": ["--check-model"],
                    }
                )
            )
            adapted = models / "00000-a.smt2.smt2"
            adapted.write_text(model_problem_text(source.read_text()))
            header = [
                "file",
                "answer",
                "outcome",
                "validator",
                "solver_exit",
                "dolmen_exit",
            ]

            def write_row(exit_code):
                with (models / "summary.csv").open("w") as stream:
                    writer = csv.writer(stream)
                    writer.writerow(header)
                    writer.writerow(
                        [str(source), "sat", "pass", "dolmen", exit_code, 0]
                    )

            write_row(0)
            self.assertEqual(
                set(model_checks(models, corpus, {"cubes/a.smt2"}, True)),
                {"cubes/a.smt2"},
            )
            adapted.write_text(adapted.read_text() + "(assert false)\n")
            with self.assertRaises(AssertionError):
                model_checks(models, corpus, {"cubes/a.smt2"})
            adapted.write_text(model_problem_text(source.read_text()))
            write_row(-11)
            with self.assertRaises(AssertionError):
                model_checks(models, corpus, {"cubes/a.smt2"})


if __name__ == "__main__":
    unittest.main()
