"""Safety checks for benchmark journals and campaign-scoped cancellation."""

import importlib.util
import csv
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
LAUNCH_SPEC = importlib.util.spec_from_file_location('long_limit_launch', FILE.with_name('launch.py'))
LAUNCH = importlib.util.module_from_spec(LAUNCH_SPEC)
LAUNCH_SPEC.loader.exec_module(LAUNCH)
REPORT_SPEC = importlib.util.spec_from_file_location('long_limit_report', FILE.with_name('report.py'))
REPORT = importlib.util.module_from_spec(REPORT_SPEC)
with patch.dict('sys.modules', {'campaign': CAMPAIGN}):
    REPORT_SPEC.loader.exec_module(REPORT)


def row(**changes):
    return {'file': 'case.smt2', 'status': 'ok', 'answer': 'sat', 'exit_code': '0',
            'timeout': 'false', 'wrong': 'false', **changes}


class LongLimitMetadataTests(unittest.TestCase):
    def test_reference_crash_remains_an_error_and_unsolved(self):
        crash = row(status='error', answer='error', exit_code=-11, seconds='1')
        good = row(file='next.smt2', seconds='2')
        CAMPAIGN.check_rows([crash, good], [], reference=True)
        result = REPORT.metrics([crash, good], 1200)
        self.assertEqual((result['errors'], result['solved'], result['par2_sum']), (1, 1, 2402))
        self.assertFalse(CAMPAIGN.decisive(crash))

    def test_reference_wrong_answers_and_disagreements_still_stop(self):
        with self.assertRaisesRegex(ValueError, 'hard answer failure'):
            CAMPAIGN.check_rows([row(wrong='true')], [], reference=True)
        with self.assertRaisesRegex(ValueError, 'disagreement'):
            CAMPAIGN.check_rows([row()], [row(answer='unsat')], reference=True)

    def test_journal_guard_isolates_reference_crash_but_stops_native_crash(self):
        for solver in ('opensmt', 'primo'):
            with self.subTest(solver=solver), tempfile.TemporaryDirectory() as directory:
                out = Path(directory)
                (out / 'answers.csv.jsonl').write_text(
                    json.dumps(row(status='error', answer='error', exit_code=-11)) + '\n'
                    + json.dumps(row(file='next.smt2')) + '\n')
                with (out / 'answers.csv').open('w', newline='') as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(row()))
                    writer.writeheader()
                    writer.writerow(row(status='error', answer='error', exit_code=-11))
                    writer.writerow(row(file='next.smt2'))
                process = SimpleNamespace(pid=123456, returncode=0, poll=lambda: 0, wait=lambda: 0)
                with patch.object(CAMPAIGN.subprocess, 'Popen', return_value=process):
                    with patch.object(CAMPAIGN, 'cancel_owned') as cancel:
                        with patch.object(CAMPAIGN.os, 'killpg'), patch.object(CAMPAIGN.time, 'sleep'):
                            selected = {'logic': 'QF_LIA', 'solver': solver, 'label': solver, 'shard': 0}
                            if solver == 'primo':
                                with self.assertRaisesRegex(ValueError, 'hard answer failure'):
                                    CAMPAIGN.guarded_benchmark([], out, selected, {'runs': []}, out, 0)
                                cancel.assert_called_once_with(out)
                            else:
                                CAMPAIGN.guarded_benchmark([], out, selected, {'runs': []}, out, 0)
                                cancel.assert_not_called()

    def test_live_journal_decode_failure_is_advisory_but_strict_read_rejects_it(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'rows.jsonl'
            path.write_text('\n')
            self.assertEqual(CAMPAIGN.journal(path, live=True), [])
            with self.assertRaises(json.JSONDecodeError):
                CAMPAIGN.journal(path)

    def test_harness_failure_does_not_cancel_other_campaign_tasks(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            process = SimpleNamespace(pid=123456, returncode=1, poll=lambda: 1, wait=lambda: 1)
            with patch.object(CAMPAIGN.subprocess, 'Popen', return_value=process):
                with patch.object(CAMPAIGN, 'cancel_owned') as cancel:
                    with patch.object(CAMPAIGN.os, 'killpg'), patch.object(CAMPAIGN.time, 'sleep'):
                        selected = {'logic': 'QF_UF', 'solver': 'z3', 'label': 'z3', 'shard': 0}
                        with self.assertRaisesRegex(RuntimeError, 'harness exited 1'):
                            CAMPAIGN.guarded_benchmark([], out, selected, {'runs': []}, out, 0)
                        cancel.assert_not_called()
            self.assertTrue((out / 'task-failures/timing-0.json').exists())
            self.assertFalse((out / 'failures').exists())

    def test_bad_live_snapshot_cannot_hide_wrong_terminal_csv(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / 'answers.csv.jsonl').write_text('\n')
            with (out / 'answers.csv').open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(row()))
                writer.writeheader()
                writer.writerow(row(wrong='true'))
            process = SimpleNamespace(pid=123456, returncode=0, poll=lambda: 0, wait=lambda: 0)
            with patch.object(CAMPAIGN.subprocess, 'Popen', return_value=process):
                with patch.object(CAMPAIGN, 'cancel_owned') as cancel:
                    with patch.object(CAMPAIGN.os, 'killpg'), patch.object(CAMPAIGN.time, 'sleep'):
                        selected = {'logic': 'QF_UF', 'solver': 'primo', 'label': 'primo', 'shard': 0}
                        with self.assertRaises(CAMPAIGN.CorrectnessFailure):
                            CAMPAIGN.guarded_benchmark([], out, selected, {'runs': []}, out, 0)
                        cancel.assert_called_once_with(out)

    def test_recovery_rejects_a_different_parent_plan(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            CAMPAIGN.write(out / 'plan.json', {})
            CAMPAIGN.write(out / 'recovery.json', {'status': 'ready', 'plan_sha256': 'foreign'})
            with self.assertRaisesRegex(ValueError, 'different frozen plan'):
                CAMPAIGN.recovery(out, {'driver_sha256': 'original'})

    def test_recovery_cannot_schedule_a_completed_task_twice(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            plan = {'driver_sha256': 'original', 'tasks': [{}, {}]}
            CAMPAIGN.write(out / 'plan.json', plan)
            CAMPAIGN.write(out / 'recovery.json', {
                'status': 'ready', 'plan_sha256': CAMPAIGN.digest(out / 'plan.json'),
                'parent_driver_sha256': 'original',
                'completed_task_indices': [0], 'pending_task_indices': [0]})
            with self.assertRaisesRegex(ValueError, 'do not partition'):
                CAMPAIGN.recovery(out, plan)

    def test_reused_shard_cannot_change_after_certification(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            run = {'logic': 'QF_LIA', 'label': 'primo', 'solver': 'primo', 'shards': 1, 'arguments': []}
            plan = {'driver_sha256': 'original', 'tools': {'primo': {'sha256': 'binary'}},
                    'config': {'timeout_seconds': 1200, 'memory_mib': 8192}, 'tasks': [run]}
            CAMPAIGN.write(out / 'plan.json', plan)
            root = out / 'timing/QF_LIA/primo/shard-0'
            CAMPAIGN.write(root / 'provenance.json', {
                'status': 'complete', 'inputs_unchanged': True, 'inputs': {'case.smt2': 'input'},
                'solver': {'sha256': 'binary'}, 'solver_args': [], 'timeout': 1200,
                'shards': 1, 'shard_index': 0})
            CAMPAIGN.write(root / 'execution-timing.json', {
                'selected': {'shard': 0, 'shards': 1}, 'plan_sha256': CAMPAIGN.digest(out / 'plan.json'),
                'driver_sha256': 'original', 'memory_mib': 8192, 'partition': 'compute',
                'cpu_models': ['Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz']})
            (root / 'answers.csv').write_text(
                'file,status,answer,exit_code,timeout,wrong\ncase.smt2,ok,sat,0,false,false\n')
            REPORT.timing_shard(out, plan, run, 0, {'case.smt2': 'input'})
            certificate = {name: CAMPAIGN.digest(root / name) for name in
                           ('provenance.json', 'execution-timing.json', 'answers.csv')}
            CAMPAIGN.write(out / 'recovery.json', {
                'status': 'ready', 'plan_sha256': CAMPAIGN.digest(out / 'plan.json'),
                'parent_driver_sha256': 'original', 'worker_driver_sha256': 'new',
                'completed_task_indices': [0], 'pending_task_indices': [],
                'reused_shards': {'QF_LIA/primo/0': certificate}})
            REPORT.timing_shard(out, plan, run, 0, {'case.smt2': 'input'})
            execution = CAMPAIGN.load(root / 'execution-timing.json')
            execution.update(driver_sha256='previous-worker', recovery_sha256='previous-recovery')
            CAMPAIGN.write(root / 'execution-timing.json', execution)
            resumed = CAMPAIGN.load(out / 'recovery.json')
            resumed['ancestor_recoveries'] = {'previous-worker': 'previous-recovery'}
            resumed['reused_shards']['QF_LIA/primo/0'] = {
                name: CAMPAIGN.digest(root / name) for name in certificate}
            CAMPAIGN.write(out / 'recovery.json', resumed)
            REPORT.timing_shard(out, plan, run, 0, {'case.smt2': 'input'})
            resumed['ancestor_recoveries']['previous-worker'] = 'changed-recovery'
            CAMPAIGN.write(out / 'recovery.json', resumed)
            with self.assertRaisesRegex(ValueError, 'immutable recovery identity'):
                REPORT.timing_shard(out, plan, run, 0, {'case.smt2': 'input'})
            resumed['ancestor_recoveries']['previous-worker'] = 'previous-recovery'
            CAMPAIGN.write(out / 'recovery.json', resumed)
            with (root / 'answers.csv').open('a') as stream:
                stream.write('other.smt2,ok,sat,0,false,false\n')
            with self.assertRaisesRegex(ValueError, 'changed or uncertified reused shard'):
                REPORT.timing_shard(out, plan, run, 0, {'case.smt2': 'input'})

    def test_stale_staging_is_rejected_before_creating_submission_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plan = root / 'plan.json'
            state = root / 'state.json'
            plan.write_text(json.dumps({'campaign_out': '/campaign',
                                        'driver_sha256': 'expected', 'config_sha256': 'config'}))
            arguments = ['launch.py', '--ssh-config', '/private/config', '--remote', 'cluster',
                         '--workspace', '/work', '--local-source', '/source',
                         '--remote-out', '/campaign', '--workflow', '/stale', '--plan', str(plan),
                         '--gate-job', '123', '--state', str(state)]
            with patch('sys.argv', arguments):
                with patch.object(LAUNCH.subprocess, 'check_output', return_value=(
                        'wrong /stale/campaign.py\nconfig /stale/config.json\n')):
                    with patch.object(LAUNCH.subprocess, 'run') as run:
                        with self.assertRaisesRegex(ValueError, 'differs from the plan'):
                            LAUNCH.main()
                        run.assert_not_called()
            self.assertFalse(state.exists())

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
