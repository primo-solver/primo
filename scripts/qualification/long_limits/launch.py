#!/usr/bin/env python3
"""Submit bounded arrays with preflights, global phase barriers and durable job IDs."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('ssh-config', 'remote', 'workspace', 'local-source', 'remote-out',
                 'workflow', 'plan', 'gate-job', 'state'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[3]
    plan_bytes = Path(args.plan).read_bytes()
    plan = json.loads(plan_bytes)
    ssh = ['ssh', '-F', args.ssh_config, args.remote]
    state_path = Path(args.state)
    if state_path.exists():
        raise ValueError('submission state already exists; inspect it rather than duplicate jobs')
    state = {'campaign_out': plan['campaign_out'],
             'plan_sha256': hashlib.sha256(plan_bytes).hexdigest(),
             'timing': [], 'models': [], 'gate_audit': args.gate_job, 'chunks': []}

    def persist():
        state_path.write_text(json.dumps(state, indent=2) + '\n')
        temporary = args.remote_out + '/jobs.json.tmp'
        subprocess.run(['scp', '-F', args.ssh_config, str(state_path),
                        args.remote + ':' + temporary], check=True)
        subprocess.run([*ssh, shlex.join(['mv', temporary, args.remote_out + '/jobs.json'])], check=True)

    def submit(phase, script, extra):
        subprocess.run([sys.executable, str(source / 'scripts/slurm_prepare_benchmark_workspace.py'),
                        '--ssh-config', args.ssh_config, '--remote', args.remote,
                        '--remote-dir', args.workspace, '--local-dir', args.local_source,
                        '--check-only', '--no-sync', '--require-tool', 'contrib/local/bin/z3',
                        '--require-tool', 'contrib/local/bin/cvc5', '--require-tool', 'contrib/local/bin/dolmen'],
                       check=True)
        check = ['test', '!', '-d', args.remote_out + '/failures']
        subprocess.run([*ssh, shlex.join(check)], check=True)
        environment = ['env', 'PRIMO_WORKDIR=' + args.workspace,
                       'PRIMO_LONG_OUT=' + args.remote_out,
                       'PRIMO_LONG_DRIVER=' + args.workflow + '/campaign.py',
                       'PRIMO_LONG_REPORT=' + args.workflow + '/report.py',
                       'PRIMO_LONG_PHASE=' + phase, 'PRIMO_LONG_STAGE=' + phase]
        command = [*environment, 'sbatch', '--parsable', '--kill-on-invalid-dep=yes',
                   '--chdir=' + args.workspace, *extra, args.workflow + '/' + script]
        job = subprocess.check_output([*ssh, shlex.join(command)], text=True).strip()
        if not job.isdecimal():
            raise ValueError('unexpected sbatch result: ' + job)
        return job

    def arrays(phase, tasks, dependency, maximum):
        chunks = [(offset, min(1000, len(tasks) - offset)) for offset in range(0, len(tasks), 1000)]
        throttle = max(1, maximum // len(chunks))
        for offset, count in chunks:
            # Per-chunk offset is exported using sbatch's structured export option.
            job = submit(phase, 'run.sbatch', [
                f'--export=ALL,PRIMO_TASK_OFFSET={offset}', f'--array=0-{count - 1}%{throttle}',
                '--dependency=afterok:' + dependency,
                '--time=' + ('15:00:00' if phase == 'models' else '09:00:00'),
                '--job-name=primo-long-' + phase])
            state[phase].append(job)
            state['chunks'].append({'phase': phase, 'job': job, 'offset': offset,
                                    'tasks': count, 'throttle': throttle})
            persist()

    persist()
    arrays('timing', plan['tasks'], args.gate_job, plan['config']['maximum_concurrent_tasks'])
    state['timing_audit'] = submit('timing', 'report.sbatch', [
        '--dependency=afterok:' + ':'.join(state['timing']), '--job-name=primo-long-timing-audit'])
    persist()
    arrays('models', plan['model_tasks'], state['timing_audit'], 64)
    state['final_report'] = submit('final', 'report.sbatch', [
        '--dependency=afterany:' + ':'.join(state['models']), '--job-name=primo-long-final-audit'])
    persist()
    print(json.dumps(state, indent=2))


if __name__ == '__main__':
    main()
