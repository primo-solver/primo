#!/usr/bin/env python3

from __future__ import annotations

import contextlib
import csv
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).parents[2] / "scripts" / "qf_uflra_smoke_fuzz.py"
SPEC = importlib.util.spec_from_file_location("qf_uflra_smoke_fuzz", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DurableSummaryTest(unittest.TestCase):
    def test_completed_rows_survive_an_interrupted_campaign(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / "out"
            argv = [
                str(SCRIPT),
                "--solver",
                "primo",
                "--ref",
                "z3",
                "--count",
                "2",
                "--out",
                str(out),
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    MODULE,
                    "run_solver",
                    side_effect=[("sat", 0), ("sat", 0), RuntimeError("interrupted")],
                ),
                contextlib.redirect_stdout(io.StringIO()),
                self.assertRaisesRegex(RuntimeError, "interrupted"),
            ):
                MODULE.main()

            with (out / "summary.csv").open(newline="", encoding="utf-8") as summary:
                rows = list(csv.DictReader(summary))
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["outcome"], "PASS")


if __name__ == "__main__":
    unittest.main()
