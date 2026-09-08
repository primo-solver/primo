import csv
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[2] / 'scripts'
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location('join_references', SCRIPTS / 'join_recorded_reference_answers.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ReferenceJoinTests(unittest.TestCase):
    def make_arm(self, root, answer='sat', digest='abc', complete=True):
        shard = root / 'shard-0'
        shard.mkdir(parents=True)
        metadata = dict(status='complete' if complete else 'running', inputs_unchanged=True,
                        inputs={'case.smt2': digest}, solver={'sha256': root.name})
        (shard / 'provenance.json').write_text(json.dumps(metadata))
        row = dict(file='case.smt2', status='ok', answer=answer, exit_code='0',
                   timeout='false', seconds='1.0')
        with (shard / 'answers.csv').open('w') as stream:
            writer = csv.DictWriter(stream, fieldnames=row)
            writer.writeheader()
            writer.writerow(row)

    def test_agreement_and_disagreement_use_recorded_answers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, answer in [('candidate', 'sat'), ('agree', 'sat'), ('wrong', 'unsat')]:
                self.make_arm(root / name, answer)
            self.assertEqual(MODULE.compare(root / 'candidate', [root / 'agree'])['status'], 'compared')
            self.assertEqual(MODULE.compare(root / 'candidate', [root / 'wrong'])['counts'],
                             {'wrong-answer': 1})
            self.assertEqual(MODULE.compare(root / 'candidate', [root / 'agree', root / 'wrong'])['counts'],
                             {'reference-disagreement': 1})

    def test_stale_hashes_rejected_and_incomplete_reference_not_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_arm(root / 'candidate')
            self.make_arm(root / 'stale', digest='different')
            self.make_arm(root / 'partial', complete=False)
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                MODULE.compare(root / 'candidate', [root / 'stale'])
            result = MODULE.compare(root / 'candidate', [root / 'partial'])
            self.assertEqual(result['status'], 'incomplete')
            self.assertEqual(result['counts'], {'no-decisive-reference': 1})


if __name__ == '__main__':
    unittest.main()
