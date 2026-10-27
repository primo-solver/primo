from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).parents[2] / 'scripts/qualification/publication_followups/budget_replay.py'
SPEC = importlib.util.spec_from_file_location('budget_replay', SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BudgetReplayTests(unittest.TestCase):
    def test_exhaustion_is_separate_and_answers_require_clean_exit(self):
        for answer, count, code, expected in (
            ('unknown', 1, 0, 'resource-exhausted'),
            ('unknown', 2, 0, 'wrong-answer-or-limit'),
            ('sat', 0, 0, 'pass'),
            ('sat', 1, 0, 'wrong-answer-or-limit'),
            ('unsat', 0, 0, 'wrong-answer-or-limit'),
            ('unknown', 0, 0, 'wrong-answer-or-limit'),
            ('sat', 0, -11, 'error'),
        ):
            with self.subTest(answer=answer, count=count, code=code), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                case = root / 'gate/case-00000/input.smt2'
                case.parent.mkdir(parents=True)
                case.write_text('(set-logic UF) (check-sat) (get-model)')
                args = ['replay', '--shard', str(root), '--slot', '20', '--count', '1',
                        '--solver', 'primo', '--reference', 'z3']
                results = [subprocess.CompletedProcess([], 0, 'sat\n', ''),
                           subprocess.CompletedProcess([], code, answer + '\n',
                                                       f':theory.conflicts {count}\n')]
                with mock.patch.object(MODULE.sys, 'argv', args), mock.patch.object(
                    MODULE.subprocess, 'run', side_effect=results
                ), contextlib.redirect_stdout(io.StringIO()):
                    status = MODULE.main()
                summary = json.loads((root / 'budget-replay/summary.json').read_text())
                self.assertEqual(summary['outcomes'], {expected: 1})
                self.assertEqual(status, int(expected not in {'pass', 'resource-exhausted'}))
                self.assertNotIn('get-model', (root / 'budget-replay/00000/input.smt2').read_text())


if __name__ == '__main__':
    unittest.main()
