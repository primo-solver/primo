import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/slurm_typed_model_mutation_gate.sbatch"
STUB = r'''
import json, os, sys
from pathlib import Path
root = Path(os.environ['PRIMO_WORKDIR'])
name, args = Path(sys.argv[0]).name, sys.argv[1:]
if name == 'primo':
    print('git: test-revision')
elif name in ('python3', 'yinyang_fuzz.sh'):
    with (root / 'calls.jsonl').open('a') as output:
        output.write(json.dumps([name, args]) + '\n')
    if name == 'yinyang_fuzz.sh':
        scratch = Path(args[args.index('--scratch') + 1])
        scratch.mkdir(parents=True)
        (scratch / 'case.smt2').write_text('(check-sat)\n')
    elif os.environ.get('TEST_SEED_FAILURE') == '1' or (
        os.environ.get('TEST_SEED_FAILURE') == 'post'
        and args[args.index('--out') + 1].endswith('mutant-answers.csv')
    ):
        sys.exit(1)
'''


class TypedMutationGateTests(unittest.TestCase):
    def run_gate(self, mutator, fail_seed=False, fail_post=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in ('build-slurm-rel/primo', 'scripts/yinyang_fuzz.sh',
                         'contrib/local/bin/python3', 'contrib/local/bin/z3',
                         'contrib/local/bin/dolmen', f'contrib/local/bin/{mutator}'):
                executable = root / path
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.write_text(f'#!{sys.executable}\n' + STUB)
                executable.chmod(0o755)
            (root / 'seeds').mkdir()
            (root / 'seeds/original.smt2').write_text('(check-sat)\n')
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith(('PRIMO_', 'SLURM_', 'TEST_SEED_'))}
            env.update(PRIMO_WORKDIR=str(root), PRIMO_EXPECTED_GIT='test-revision',
                       PRIMO_MODEL_SEEDS='seeds', PRIMO_FUZZ_OUT_DIR='out',
                       PRIMO_MUTATION_TOOL=mutator, SLURM_ARRAY_TASK_ID='2',
                       PRIMO_MUTATION_SEED_TIMEOUT='60',
                       PRIMO_EXTRA_SOLVER_ARGS='--no-qf-uflia-model-combination',
                       TEST_SEED_FAILURE='post' if fail_post else str(int(fail_seed)))
            result = subprocess.run(['bash', str(SCRIPT)], env=env,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, int(fail_seed or fail_post), result.stdout + result.stderr)
            return [json.loads(line) for line in (root / 'calls.jsonl').read_text().splitlines()]

    def test_options_reach_originals_mutation_and_models(self):
        expected = ['--quiet', '--check-model', '--preprocessing-mode=strong',
                    '--no-qf-uflia-model-combination']
        for mutator in ('typefuzz', 'opfuzz'):
            with self.subTest(mutator=mutator):
                calls = self.run_gate(mutator)
                self.assertEqual(len(calls), 5)
                for index in (0, 1):
                    self.assertEqual(calls[index][1][calls[index][1].index('--timeout') + 1], '60')
                for index in (3, 4):
                    self.assertEqual(calls[index][1][calls[index][1].index('--timeout') + 1], '5')
                for name, args in calls:
                    if name == 'python3':
                        self.assertEqual([arg.split('=', 1)[1] for arg in args
                                          if arg.startswith('--solver-arg=')], expected)
                    else:
                        self.assertEqual(shlex.split(args[args.index('--solver') + 1])[1:], expected)
                        executable = args[args.index('--typefuzz') + 1]
                        self.assertTrue(executable.endswith('scripts/seeded_opfuzz.py')
                                        if mutator == 'opfuzz' else executable == 'typefuzz')
                        self.assertEqual('--config' in args, mutator == 'opfuzz')

    def test_seed_failure_prevents_mutation(self):
        calls = self.run_gate('opfuzz', fail_seed=True)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][1][0], 'scripts/check_solver_seed_corpus.py')

    def test_post_comparison_failure_is_not_masked_by_passing_models(self):
        calls = self.run_gate('opfuzz', fail_post=True)
        self.assertEqual(len(calls), 5)
        self.assertEqual(calls[-1][1][0], 'scripts/validate_model.py')


if __name__ == '__main__':
    unittest.main()
