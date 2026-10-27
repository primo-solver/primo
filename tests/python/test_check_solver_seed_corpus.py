import contextlib
import csv
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[2] / 'scripts/check_solver_seed_corpus.py'
SPEC = importlib.util.spec_from_file_location('check_solver_seed_corpus', SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SeedStatusTests(unittest.TestCase):
    def test_mutations_ignore_inherited_status_only_when_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'case.smt2').write_text('(set-info :status unsat)\n(check-sat)\n')
            output = root / 'answers.csv'
            arguments = ['check', '--seeds', directory, '--solver', 'candidate',
                         '--ref', 'reference', '--out', str(output)]
            for extra, code, outcome in [([], 1, 'STATUS_MISMATCH'),
                                         (['--ignore-declared-status'], 0, 'PASS')]:
                with patch('sys.argv', arguments + extra), \
                     patch.object(MODULE, 'run', return_value=('sat', 0)), \
                     contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(MODULE.main(), code)
                with output.open() as stream:
                    self.assertEqual(next(csv.DictReader(stream))['outcome'], outcome)


if __name__ == '__main__':
    unittest.main()
