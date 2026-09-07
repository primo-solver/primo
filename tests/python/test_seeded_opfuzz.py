import importlib.util
from pathlib import Path
import random
import sys
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    'seeded_opfuzz', Path(__file__).resolve().parents[2] / 'scripts/seeded_opfuzz.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SeededOpfuzzTests(unittest.TestCase):
    def setUp(self):
        self.addCleanup(random.setstate, random.getstate())

    def test_seeds_before_loading_and_preserves_external_arguments(self):
        def run(path, run_name):
            self.assertEqual((path, run_name), ('/installed/opfuzz', '__main__'))
            self.assertEqual(random.random(), random.Random(17).random())
        with patch.object(module.shutil, 'which', return_value='/installed/opfuzz'), \
             patch.object(module.runpy, 'run_path', side_effect=run), \
             patch.object(sys, 'argv', ['wrapper', '-kl', '--seed', '17', '-i', '2', 'solver', 'seeds']):
            self.assertEqual(module.main(), 0)
            self.assertEqual(sys.argv, ['/installed/opfuzz', '-kl', '-i', '2', 'solver', 'seeds'])

    def test_external_failure_propagates(self):
        with patch.object(module.shutil, 'which', return_value='/installed/opfuzz'), \
             patch.object(module.runpy, 'run_path', side_effect=SystemExit(23)), \
             patch.object(sys, 'argv', ['wrapper', '--seed', '17']):
            with self.assertRaises(SystemExit) as error:
                module.main()
            self.assertEqual(error.exception.code, 23)


if __name__ == '__main__':
    unittest.main()
