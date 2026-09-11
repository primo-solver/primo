#!/usr/bin/env python3
"""Exercise real child-process error collection without accepting provisional answers."""

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

DRIVER = Path(__file__).parents[2] / "scripts" / "native_uf_sessions.py"


class ReferenceErrorsTest(unittest.TestCase):
    def run_driver(self, keep_going, first_answer="sat", expected_git=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            solver = root / "fake-reference"
            solver.write_text(
                f"#!{sys.executable}\nimport sys\n"
                "if '--version' in sys.argv:\n"
                " print('reference-test', file=sys.stderr); sys.exit(0)\n"
                "text = sys.stdin.read()\n"
                f"print({first_answer!r} if 'crash' in text else 'sat')\n"
                "sys.exit(7 if 'crash' in text else 0)\n")
            solver.chmod(0o755)
            entries = []
            for index, text in enumerate(("crash", "pass")):
                source = root / f"case-{index}.smt2"
                source.write_text(text)
                entries.append({"file": source.name, "answers": ["sat"], "queries": 1,
                                "sha256": hashlib.sha256(source.read_bytes()).hexdigest()})
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({"entries": entries}))
            out = root / "output"
            command = [sys.executable, str(DRIVER), "timing", "--root", str(root),
                       "--manifest", str(manifest), "--solver", str(solver),
                       "--out", str(out), "--timeout", "2"]
            if keep_going:
                command.append("--continue-after-error")
            if expected_git:
                command.extend(["--expected-git", "candidate"])
            run = subprocess.run(command, capture_output=True, text=True, timeout=10)
            rows = json.loads((out / "summary.json").read_text()) if out.exists() else []
            metadata = json.loads((out / "run.json").read_text()) if out.exists() else {}
            return run, rows, metadata

    def test_default_stops_after_post_answer_error(self):
        run, rows, _ = self.run_driver(False)
        self.assertEqual(run.returncode, 1)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["exit_code"], 7)
        self.assertEqual(rows[0]["outcome"], "error")
        self.assertEqual(rows[0]["par2"], 4)

    def test_reference_collection_continues_but_fails_overall(self):
        run, rows, metadata = self.run_driver(True)
        self.assertEqual(run.returncode, 1)
        self.assertEqual([r["outcome"] for r in rows], ["error", "complete"])
        self.assertEqual(metadata["version"], "reference-test\n")

    def test_wrong_answers_still_stop(self):
        run, rows, _ = self.run_driver(True, "unsat")
        self.assertEqual(run.returncode, 1)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["outcome"], "wrong-answer")

    def test_candidate_cannot_enable_reference_error_collection(self):
        run, rows, _ = self.run_driver(True, expected_git=True)
        self.assertEqual(run.returncode, 2)
        self.assertEqual(rows, [])


if __name__ == "__main__":
    unittest.main()
