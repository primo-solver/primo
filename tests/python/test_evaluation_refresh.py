"""Reject stale timing identities before producing a fresh baseline report."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'evaluation_refresh', ROOT / 'scripts/qualification/evaluation_refresh/summarize.py')
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


class EvaluationRefreshTests(unittest.TestCase):
    def test_stale_binary_input_options_and_timeout_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shard = root / 'shard-0'
            shard.mkdir()
            original = {'solver': {'sha256': 'current'}, 'solver_args': ['--quiet'],
                        'timeout': 60, 'inputs': {'a.smt2': 'input'}}
            for change in ({}, {'solver': {'sha256': 'old'}}, {'solver_args': []},
                           {'timeout': 30}, {'inputs': {'a.smt2': 'changed'}},
                           {'inputs': {}}, {'inputs': {'other.smt2': 'input'}}):
                with self.subTest(change=change):
                    (shard / 'provenance.json').write_text(json.dumps(original | change))
                    if change:
                        with self.assertRaises(ValueError):
                            REPORT.verify_provenance(root, {'a.smt2': {'sha256': 'input'}},
                                                     'current', ['--quiet'], 60)
                    else:
                        REPORT.verify_provenance(root, {'a.smt2': {'sha256': 'input'}},
                                                 'current', ['--quiet'], 60)

    def test_duplicate_shards_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            row = {'solver': {'sha256': 'current'}, 'solver_args': [], 'timeout': 60,
                   'inputs': {'a.smt2': 'input'}}
            for index in range(2):
                shard = root / f'shard-{index}'
                shard.mkdir()
                (shard / 'provenance.json').write_text(json.dumps(row))
            with self.assertRaises(ValueError):
                REPORT.verify_provenance(root, {'a.smt2': {'sha256': 'input'}},
                                         'current', [], 60)


if __name__ == '__main__':
    unittest.main()
