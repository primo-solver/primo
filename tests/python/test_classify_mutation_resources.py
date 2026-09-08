import importlib.util
from pathlib import Path
import unittest


SOURCE = Path(__file__).resolve().parents[2] / 'scripts/classify_mutation_resources.py'
SPEC = importlib.util.spec_from_file_location('classify_mutation_resources', SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class MutationResourceTests(unittest.TestCase):
    def test_only_explicit_resource_rows_permit_collection(self):
        for answer, code in [('timeout', '124'), ('unknown', '0')]:
            row = dict(outcome='INCOMPLETE', ours=answer, ours_exit=code,
                       reference='sat', reference_exit='0')
            self.assertTrue(MODULE.resource_only([dict(outcome='PASS'), row], 'answers'))
            row['ours_exit'] = '1'
            self.assertFalse(MODULE.resource_only([row], 'answers'))

    def test_errors_and_disagreements_block_even_alongside_timeout(self):
        resource = dict(outcome='INCOMPLETE', ours='timeout', ours_exit='124',
                        reference='sat', reference_exit='0')
        for outcome in ['DISAGREEMENT', 'STATUS_MISMATCH', 'INCOMPLETE']:
            failure = dict(outcome=outcome, ours='crash', ours_exit='1',
                           reference='sat', reference_exit='0')
            self.assertFalse(MODULE.resource_only([resource, failure], 'answers'))

    def test_models_fail_closed_except_resources(self):
        for outcome in ['solver-timeout', 'solver-unknown', 'dolmen-timeout',
                        'dolmen-resource-limit']:
            self.assertTrue(MODULE.resource_only([dict(outcome=outcome)], 'models'))
        for outcome in ['model-error', 'solver-error', 'input-nonconforming',
                        'validator-error', 'answer-changed', 'validation-unavailable']:
            self.assertFalse(MODULE.resource_only([dict(outcome='solver-timeout'),
                                                  dict(outcome=outcome)], 'models'))

    def test_missing_and_falsely_failed_complete_results_are_rejected(self):
        self.assertFalse(MODULE.resource_only([], 'answers'))
        self.assertFalse(MODULE.resource_only([dict(outcome='PASS')], 'answers'))
        self.assertFalse(MODULE.resource_only([dict(outcome='pass')], 'models'))


if __name__ == '__main__':
    unittest.main()
