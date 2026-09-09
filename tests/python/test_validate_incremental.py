import argparse
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import validate_incremental as gate


class IncrementalValidationTests(unittest.TestCase):
    def test_scoped_snapshots_include_declarations(self):
        journal = gate.QueryJournal()
        text = "(set-logic QF_UF)(declare-const a Bool)(push 1)(define-fun b () Bool a)(pop 1)"
        for start, end, head in gate.command_spans(text):
            journal.apply(text[start:end], head)
        self.assertIn("declare-const a", journal.snapshot())
        self.assertNotIn("define-fun b", journal.snapshot())
        journal.apply("(reset-assertions)", ("reset-assertions",))
        self.assertEqual(journal.snapshot(), "(set-logic QF_UF)\n(check-sat)")

    def test_response_framing(self):
        for text in [
            "()",
            "(model (define-fun |x)| () Bool true))",
            '(error "a "" ) "" b")',
        ]:
            self.assertEqual(gate.response_end(text), len(text))
            self.assertIsNone(gate.response_end(text[:-1]))

    def test_post_answer_crash_cannot_pass(self):
        args = argparse.Namespace(
            solver="fake",
            solver_arg=[],
            ref="fake-ref",
            ref_arg=[],
            dolmen="fake-dolmen",
            timeout=1,
            dolmen_timeout=1,
        )
        with (
            tempfile.TemporaryDirectory() as temporary,
            patch.object(gate, "Client") as client,
        ):
            client.return_value.response.return_value = "unsat"
            client.return_value.finish.side_effect = ValueError(
                "solver exited 1 after its answers"
            )
            with patch.object(
                gate, "run_command", return_value=("done", 0, "unsat\n", "", 0)
            ):
                result = gate.validate(
                    "(set-logic QF_UF)(assert false)(check-sat)",
                    Path(temporary) / "run",
                    args,
                )
            self.assertEqual(result["outcome"], "failure")
            self.assertNotEqual(result["queries"][0]["outcome"], "pass")

    def test_reference_nonzero_cannot_pass(self):
        args = argparse.Namespace(
            solver="fake",
            solver_arg=[],
            ref="fake-ref",
            ref_arg=[],
            dolmen="fake-dolmen",
            timeout=1,
            dolmen_timeout=1,
        )
        with (
            tempfile.TemporaryDirectory() as temporary,
            patch.object(gate, "Client") as client,
        ):
            client.return_value.response.return_value = "unsat"
            with patch.object(
                gate, "run_command", return_value=("done", 1, "unsat\n", "", 0)
            ):
                result = gate.validate(
                    "(set-logic QF_UF)(assert false)(check-sat)",
                    Path(temporary) / "run",
                    args,
                )
            self.assertEqual(result["outcome"], "failure")


if __name__ == "__main__":
    unittest.main()
