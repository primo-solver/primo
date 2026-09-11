import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from profile_native_uf_gaps import reference_problem, reference_result


class NativeUfProfileTests(unittest.TestCase):
    def test_statistics_insertion_preserves_strings_and_trailing_comments(self):
        original = '(set-info :source "(check-sat)")\n; (check-sat)\n(check-sat); tail\n(exit)'
        expected = ('(set-info :source "(check-sat)")\n; (check-sat)\n'
                    '(check-sat)\n(get-info :all-statistics)\n; tail\n(exit)')
        self.assertEqual(reference_problem(original), expected)

    def test_statistics_do_not_hide_exit_errors_or_missing_query_data(self):
        stdout = 'sat\n(:all-statistics (:conflicts 12))\n'
        self.assertEqual(reference_result(stdout, 7, False, ['sat'])['outcome'], 'error')
        self.assertEqual(reference_result(stdout, 0, True, ['sat'])['outcome'], 'timeout')
        self.assertEqual(reference_result(stdout + '(error "failure")', 0, False,
                                          ['sat'])['outcome'], 'error')
        self.assertEqual(reference_result('sat\n', 0, False, ['sat'])['outcome'], 'error')
        self.assertEqual(reference_result('unsat\n', 0, False, ['sat'])['outcome'],
                         'wrong-answer')
        self.assertEqual(reference_result('sat\n', 0, True, ['sat'])['outcome'], 'timeout')
        result = reference_result(stdout, 0, False, ['sat'])
        self.assertEqual(result['outcome'], 'complete')
        self.assertEqual(result['reference_statistics'], [{'conflicts': '12'}])


if __name__ == '__main__':
    unittest.main()
