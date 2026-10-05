"""Reject stale timing identities before producing a fresh baseline report."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'evaluation_refresh', ROOT / 'scripts/qualification/evaluation_refresh/summarize.py')
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


class EvaluationRefreshTests(unittest.TestCase):
    def test_historical_selection_never_uses_cancelled_fresh_reference_rows(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inventory = root / 'inventory.json'
            references = root / 'references.json'
            output = root / 'report.json'
            inventory.write_text(json.dumps({
                'corpora': {'QF_UF': {'count': 1, 'inputs': {'a': {'sha256': 'input'}}}},
                'tools': {label: {'sha256': label} for label in ('primo', 'z3', 'cvc5')}}))
            references.write_text(json.dumps({'QF_UF': {'z3': {
                'root': str(root / 'historical-z3'), 'measurement_note': '60s, one CPU'}}}))
            def load(folder, count, timeout):
                self.assertNotIn(folder, (root / 'QF_UF/z3', root / 'QF_UF/cvc5'))
                row = {'status': 'ok', 'answer': 'sat', 'exit_code': '0',
                       'timeout': 'false', 'seconds': '1'}
                return {'timing_complete': True, 'par2': 1, 'exception_rows': []}, {'a': row}
            argv = ['summarize', '--root', str(root), '--inventory', str(inventory),
                    '--reference-map', str(references), '--out', str(output)]
            with patch.object(REPORT.sys, 'argv', argv), \
                    patch.object(REPORT, 'verify_provenance') as verify, \
                    patch.object(REPORT, 'load_run', side_effect=load):
                self.assertEqual(REPORT.main(), 0)
                self.assertEqual(verify.call_count, 2)
                self.assertEqual(verify.call_args.args[0], root / 'historical-z3')
            route = json.loads(output.read_text())['routes']['QF_UF']
            self.assertEqual(route['unavailable_references'], ['cvc5'])
            self.assertEqual(route['runs']['z3']['timing_origin'], 'historical')
            self.assertEqual(route['runs']['primo']['timing_origin'], 'fresh')

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
