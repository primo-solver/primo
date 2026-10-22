"""Safety checks for benchmark journals and campaign-scoped cancellation."""

import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

FILE = Path(__file__).resolve().parents[2] / 'scripts/qualification/long_limits/campaign.py'
SPEC = importlib.util.spec_from_file_location('long_limit_campaign', FILE)
CAMPAIGN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAMPAIGN)


def row(**changes):
    return {'file': 'case.smt2', 'status': 'ok', 'answer': 'sat', 'exit_code': '0',
            'timeout': 'false', 'wrong': 'false', **changes}


class LongLimitMetadataTests(unittest.TestCase):
    def test_small_manifests_cover_the_same_inputs_for_every_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            config = {'corpora': {'QF_LRA': 3}, 'references': ['z3'],
                      'native_profiles': {'primo': ['--quiet'], 'primo-phase-mono': ['--quiet', '--lra-model-phase']},
                      'maximum_inputs_per_shard': 2}
            config_path = out / 'config.json'
            config_path.write_text(json.dumps(config))
            CAMPAIGN.write(out / 'tools.json', {})
            CAMPAIGN.write(out / 'capabilities.json', {'logics': {'QF_LRA': {'z3': {'supported': True}}}})
            CAMPAIGN.write(out / 'inventory/QF_LRA.json', {'count': 3, 'inputs': {
                'a.smt2': {'sha256': 'a'}, 'b.smt2': {'sha256': 'b'}, 'c.smt2': {'sha256': 'c'}}})
            CAMPAIGN.plan(SimpleNamespace(config=config_path), config, out)
            plan = CAMPAIGN.load(out / 'plan.json')
            self.assertEqual(len(plan['tasks']), 6)
            self.assertEqual(len(plan['model_tasks']), 4)
            for label in ('primo', 'primo-phase-mono', 'z3'):
                names = []
                for task in plan['tasks']:
                    if task['label'] == label:
                        selected = Path(task['manifest']).read_text().splitlines()
                        self.assertLessEqual(len(selected), 2)
                        names.extend(selected)
                self.assertEqual(sorted(names), ['a.smt2', 'b.smt2', 'c.smt2'])

    def test_partial_journal_line_is_not_an_answer(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'answers.jsonl'
            path.write_text(json.dumps(row()) + '\n' + '{"file":')
            self.assertEqual(CAMPAIGN.journal(path), [row()])

    def test_numeric_exit_code_in_durable_journal_is_success(self):
        CAMPAIGN.check_rows([row(exit_code=0)], [])
        self.assertTrue(CAMPAIGN.decisive(row(exit_code=0)))

    def test_timeout_and_unknown_do_not_create_disagreement(self):
        CAMPAIGN.check_rows([row()], [row(answer='unknown')])
        CAMPAIGN.check_rows([row()], [row(status='timeout', answer='unsat', timeout='true')])

    def test_opposite_successful_answer_is_a_failure(self):
        with self.assertRaisesRegex(ValueError, 'disagreement'):
            CAMPAIGN.check_rows([row()], [row(answer='unsat')])

    def test_error_after_printed_answer_is_not_a_solve(self):
        with self.assertRaisesRegex(ValueError, 'hard answer failure'):
            CAMPAIGN.check_rows([row(exit_code='1')], [])

    def test_cancellation_rejects_foreign_campaign(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / 'plan.json').write_text('{}\n')
            (out / 'jobs.json').write_text(json.dumps({'campaign_out': '/different'}))
            with patch.dict('os.environ', {'SLURM_ARRAY_JOB_ID': '123'}):
                with patch.object(CAMPAIGN.subprocess, 'run') as run:
                    with self.assertRaisesRegex(ValueError, 'different campaign'):
                        CAMPAIGN.cancel_owned(out)
                    run.assert_not_called()

    def test_cancellation_only_uses_recorded_campaign_arrays(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            plan = out / 'plan.json'
            plan.write_text('{}\n')
            (out / 'jobs.json').write_text(json.dumps({
                'campaign_out': str(out), 'plan_sha256': CAMPAIGN.digest(plan),
                'timing': ['123', '124'], 'models': ['125']}))
            with patch.dict('os.environ', {'SLURM_ARRAY_JOB_ID': '123'}):
                with patch.object(CAMPAIGN.subprocess, 'run') as run:
                    CAMPAIGN.cancel_owned(out)
                    run.assert_called_once_with(['scancel', '123', '124', '125'], check=False)


if __name__ == '__main__':
    unittest.main()
