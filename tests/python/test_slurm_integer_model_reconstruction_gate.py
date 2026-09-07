from __future__ import annotations

import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest import mock


SCRIPT = (
    Path(__file__).parents[2] / "scripts" / "slurm_integer_model_reconstruction_gate.sbatch"
)


class IntegerModelGateTests(unittest.TestCase):
    def test_summary_acceptance_distinguishes_limits_from_failures(self) -> None:
        source = SCRIPT.read_text().split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
        scenarios = (
            ("valid", "PASS\nPASS\n", "pass\nnot-sat\n", 0, 0),
            ("limit", "PASS\nUNKNOWN_INVOLVED\n", "pass\nsolver-unknown\n", 1, 0),
            ("wrong model", "PASS\nPASS\n", "pass\nmodel-error\n", 1, 1),
            ("incomplete", "PASS\nPASS\n", "pass\n", 0, 1),
            ("bad exit", "PASS\nPASS\n", "pass\nnot-sat\n", 2, 1),
            ("false exit", "PASS\nPASS\n", "pass\nnot-sat\n", 1, 1),
            ("wrong answer", "PASS\nOUR_SAT_REF_UNSAT\n", "pass\nnot-sat\n", 0, 1),
            ("bad input", "PASS\nPASS\n", "pass\ninput-nonconforming\n", 0, 1),
        )
        for label, answers, models, status, expected in scenarios:
            with self.subTest(label=label):
                def opened(path: Path, *args: object, **kwargs: object) -> io.StringIO:
                    rows = answers if path.parent.name == "answer" else models
                    return io.StringIO("outcome\n" + rows)

                with (
                    mock.patch.object(Path, "open", opened),
                    mock.patch.object(Path, "write_text"),
                    mock.patch.object(sys, "argv", ["gate", "/tmp/gate", "2", "1", str(status)]),
                    contextlib.redirect_stdout(io.StringIO()),
                    self.assertRaises(SystemExit) as caught,
                ):
                    exec(compile(source, "gate-heredoc", "exec"), {})
                self.assertEqual(caught.exception.code, expected)


if __name__ == "__main__":
    unittest.main()
