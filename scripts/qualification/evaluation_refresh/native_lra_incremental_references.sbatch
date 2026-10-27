#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-incremental-refs
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --hint=nomultithread
#SBATCH --array=0-5%6
#SBATCH --output=results/lra-incremental-refs-%A_%a.out
#SBATCH --error=results/lra-incremental-refs-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, 'scripts')
from validate_incremental import validate
from validate_qfuf_models_with_dolmen import run_command, write_json

slot = int(os.environ['SLURM_ARRAY_TASK_ID'])
index = [6, 8][slot % 2]
label, options = [
    ('yices-smt2', ['--incremental']),
    ('opensmt', []),
    ('cvc5', ['--incremental', '--use-portfolio', '-L', 'smt2.6',
              '--print-success', '--no-type-checking', '--no-interactive', '--fp-exp']),
][slot // 2]
root = Path('results/refresh-native-lra/incremental-references') / label / f'shard-{index}'
root.mkdir(parents=True, exist_ok=False)
manifest = Path('results/native-lra-manifest.json')
entry = json.loads(manifest.read_text())['entries'][index]
source = Path(os.environ['PRIMO_LRA_ROOT']) / entry['file']
solver = Path('build-slurm-rel/primo')
reference = Path('contrib/local/bin') / label
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(source) == entry['sha256']
assert sha(solver) == 'd0f69f50540efa7561e7895d2f24acff26787756a85b9bdb9f67e7eaaed496e0'
text = source.read_text()
(root / 'original.smt2').write_text(text)
command = [str(reference), *options, str(source)]
prior_path = Path(f'results/refresh-native-lra/timing/assumptions/shard-{index}/session-{index:04d}/result.json')
prior = json.loads(prior_path.read_text())
assert prior['outcome'] == 'complete'
write_json(root / 'run.json', {
    'job': os.environ['SLURM_JOB_ID'], 'host': platform.node(), 'index': index,
    'source_sha256': sha(source), 'manifest_sha256': sha(manifest),
    'solver_sha256': sha(solver), 'reference_sha256': sha(reference),
    'reference_version': subprocess.check_output([str(reference), '--version'],
        stderr=subprocess.STDOUT, text=True, timeout=10),
    'command': command, 'reference_whole_session_timeout': 600,
    'candidate_query_timeout': 600, 'dolmen_timeout': 60,
    'timing_result_sha256': sha(prior_path),
    'scope': 'Correctness only: full incremental reference then live Primo model replay.',
})
status, code, stdout, stderr, seconds = run_command(command, 600)
(root / 'reference.stdout').write_text(stdout)
(root / 'reference.stderr').write_text(stderr)
lines = [line.strip() for line in stdout.splitlines() if line.strip()]
# cvc5 --print-success acknowledges commands. Preserve raw output and accept
# only exact success tokens in addition to the query answer sequence.
answers = [line for line in lines if line != 'success']
reference_ok = (status == 'done' and code == 0 and
                len(answers) == entry['queries'] and
                all(answer in {'sat', 'unsat'} for answer in answers))
result = {'index': index, 'file': entry['file'], 'reference_status': status,
          'reference_exit_code': code, 'reference_seconds': seconds,
          'reference_answers': answers, 'outcome': 'reference-incomplete'}
if reference_ok:
    if answers != prior['answers']:
        result['outcome'] = 'answer-disagreement'
    else:
        args = SimpleNamespace(
            solver=str(solver), solver_arg=['--quiet', '--stats',
                '--max-theory-conflicts=10000', '--incremental-mode=assumptions'],
            ref=str(reference), ref_arg=options, dolmen='contrib/local/bin/dolmen',
            timeout=600, dolmen_timeout=60)
        replay = validate(text, root / 'candidate-replay', args, expected_answers=answers)
        result.update(replay)
write_json(root / 'summary.json', [result])
print(json.dumps({'reference': label, 'index': index, 'outcome': result['outcome'],
                  'queries': len(result.get('queries', [])),
                  'message': result.get('message')}))
raise SystemExit(0 if result['outcome'] == 'pass' else 1)
PY
