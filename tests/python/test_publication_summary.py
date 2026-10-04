"""Resource outcomes remain incomplete in the publication artifact verifier."""

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'publication_summary', ROOT / 'scripts/qualification/summarize_public_launch.py')
SUMMARY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUMMARY)


class PublicationSummaryTests(unittest.TestCase):
    def test_reference_timeout_is_incomplete_and_errors_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'exit-code').write_text('0\n')
            (folder / 'version.txt').write_text('git: 1234567\n')
            (folder / 'executables.sha256').write_text('a primo\nb z3\nc dolmen\n')
            rows = [dict(file=str(folder / f'{index}.smt2'), ours='unsat', refs='unsat',
                         outcome='PASS') for index in range(250)]
            for reference, outcome, accepted in (
                ('TIMEOUT', 'TIMEOUT', True), ('sat', 'TIMEOUT', True),
                ('ERROR', 'TIMEOUT', False), ('sat', 'WRONG', False),
            ):
                with self.subTest(reference=reference, outcome=outcome):
                    rows[0].update(ours='TIMEOUT', refs=reference, outcome=outcome)
                    with patch.object(SUMMARY, 'read_csv', return_value=rows), \
                            patch.object(SUMMARY, 'digest', return_value='hash'):
                        if not accepted:
                            with self.assertRaises(ValueError):
                                SUMMARY.summarize_shard(folder, 8, '1234567')
                        else:
                            result = SUMMARY.summarize_shard(folder, 8, '1234567')
                            self.assertEqual(result['pass'], 249)
                            self.assertEqual(result['timeout'], 1)
                            self.assertEqual(result['reference_timeout'],
                                             int(reference == 'TIMEOUT'))

    def test_unknown_requires_exact_hash_bound_triage(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'exit-code').write_text('0\n')
            (folder / 'version.txt').write_text('git: 1234567\n')
            (folder / 'executables.sha256').write_text('a primo\nb z3\nc dolmen\n')
            rows = [dict(file=str(folder / f'{index}.smt2'), ours='unsat', refs='unsat',
                         outcome='PASS', ours_code='0') for index in range(250)]
            rows[0].update(ours='unknown', refs='sat', outcome='UNKNOWN_INVOLVED')
            triage = {'hash': {'route': 'QF_UFLIA', 'reference_answer': 'sat'}}
            for manifest, exit_code, accepted in (
                (None, '0', False), ({'other': triage['hash']}, '0', False),
                (triage, '1', False), (triage, '0', True),
            ):
                with self.subTest(manifest=manifest, exit_code=exit_code):
                    rows[0]['ours_code'] = exit_code
                    with patch.object(SUMMARY, 'read_csv', return_value=rows), \
                            patch.object(SUMMARY, 'digest', return_value='hash'):
                        if accepted:
                            result = SUMMARY.summarize_shard(folder, 16, '1234567', manifest)
                            self.assertEqual(result['pass'], 249)
                            self.assertEqual(result['resource_incomplete'], 1)
                            self.assertEqual(result['resource_triage'], ['hash'])
                        else:
                            with self.assertRaises(ValueError):
                                SUMMARY.summarize_shard(folder, 16, '1234567', manifest)


if __name__ == '__main__':
    unittest.main()
