import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import incremental_lra_gate as gate
from validate_incremental import QueryJournal


class LraGateTests(unittest.TestCase):
    def test_failed_session_is_recorded_and_stops_campaign(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "primo"
            binary.write_bytes(b"test binary identity")
            output = root / "gate"
            arguments = [
                "incremental_lra_gate.py",
                "--kind",
                "generated",
                "--seed",
                "7",
                "--count",
                "2",
                "--solver",
                str(binary),
                "--ref",
                "z3",
                "--dolmen",
                "dolmen",
                "--expected-git",
                "candidate",
                "--out",
                str(output),
            ]
            failure = {
                "outcome": "failure",
                "message": "solver exited with code 1",
                "queries": [{"answer": "sat", "outcome": "provisional-pass"}],
            }
            with (
                mock.patch.object(sys, "argv", arguments),
                mock.patch.object(
                    gate.subprocess, "check_output", return_value="git: candidate\n"
                ),
                mock.patch.object(gate, "validate", return_value=failure) as validate,
                mock.patch("builtins.print", lambda *args: None),
            ):
                self.assertEqual(gate.main(), 1)
            validate.assert_called_once()
            summary = json.loads((output / "summary.json").read_text())
            self.assertEqual(summary["untriaged_failure"], 1)
            self.assertEqual(summary["pass"], 0)
            self.assertEqual(summary["sat_models"], 0)
            self.assertEqual(summary["failures"], [{"seed": 7, "result": failure}])

    def test_generated_scope_balance_and_diversity(self):
        sessions = set()
        for seed in range(30):
            text = gate.generated(seed)
            journal = QueryJournal()
            queries = 0
            for start, end, head in gate.command_spans(text):
                journal.apply(text[start:end], head)
                queries += head[0] == "check-sat"
            self.assertEqual(len(journal.frames), 1)
            self.assertEqual(queries, 11)
            self.assertNotIn("QF_UF", text)
            sessions.add(text)
        self.assertEqual(len(sessions), 30)

    def test_native_mutations_preserve_names_and_scope_commands(self):
        source = (
            "(set-logic QF_LRA)(declare-fun n.1__AT0 () Real)"
            "(assert (<= n.1__AT0 3.5))(push 2)(assert (> n.1__AT0 2))(check-sat)"
        )
        commands = [(source[a:b], h) for a, b, h in gate.command_spans(source)]
        for seed in range(20):
            text = gate.mutate_native(commands, seed)
            self.assertIn("n.1__AT0", text)
            self.assertIn("(push 2)", text)
            self.assertIn("(pop 2)", text)
            journal = QueryJournal()
            for a, b, head in gate.command_spans(text):
                journal.apply(text[a:b], head)
            self.assertEqual(len(journal.frames), 1)


if __name__ == "__main__":
    unittest.main()
