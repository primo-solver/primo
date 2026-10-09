#!/usr/bin/env bash
# Fresh automatic-default qualification: six 500-input answer/model shards
# and four 250-input Boolean/arithmetic shards. SAT replay alone broadens
# Dolmen's logic to QF_NRA; this checks semantics, not strict QF_LRA syntax.
# Submit seed mutation separately and independently replay retained mutants.
#SBATCH --job-name=primo-lra-default-gate
#SBATCH --partition=compute
#SBATCH --array=0-9%8
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=01:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
task=${SLURM_ARRAY_TASK_ID:?}
base=${PRIMO_CAMPAIGN_OUT:?}
expected=${PRIMO_EXPECTED_GIT:?}
seed=$((2026101200 + task))
out="$base/task-$task"
mkdir -p "$out" logs
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: $expected" "$out/version.txt"
python3 - "$out" "$seed" "$task" <<'IDENTITY'
import hashlib,json,pathlib,subprocess,sys
out=pathlib.Path(sys.argv[1])
binary=pathlib.Path('build-slurm-rel/primo')
data={'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
      'version':(out/'version.txt').read_text(),'solver_args':['--quiet'],
      'seed':int(sys.argv[2]),'task':int(sys.argv[3]),
      'host':subprocess.check_output(['hostname'],text=True).strip(),
      'timeout_seconds':5,'reference_command':['contrib/local/bin/z3'],
      'model_stage':'separate direct Dolmen replay of successful SAT answers'}
(out/'identity.json').write_text(json.dumps(data,indent=2)+'\n')
IDENTITY
if (( task < 6 )); then
  python3 scripts/fuzz_external.py --fuzzer fuzzsmt \
    --solver 'build-slurm-rel/primo --quiet' --refs z3 --logic QF_LRA \
    --count 500 --timeout 5 --seed "$seed" \
    --fuzzer-args '-ms 1 -Ms 3 -mv 1 -Mv 3 -mf 1 -mp 1 -ma 1 -Ma 2' \
    --out "$out/answers"
  replay_status=0
  python3 scripts/check_solver_seed_corpus.py --seeds "$out/answers/generated" \
    --solver build-slurm-rel/primo --solver-arg=--quiet \
    --ref contrib/local/bin/z3 --timeout 5 --out "$out/status-aware-answers.csv" \
    || replay_status=$?
  if (( replay_status > 1 )); then exit "$replay_status"; fi
  python3 - "$out" <<'SELECT'
import collections,csv,hashlib,json,pathlib,sys
out=pathlib.Path(sys.argv[1])
summary=out/'answers/summary.csv'
rows=list(csv.DictReader(summary.open()))
counts=dict(collections.Counter(row['outcome'] for row in rows))
if len(rows)!=500 or any(row['outcome'] not in ('PASS','TIMEOUT') for row in rows):
    raise SystemExit(f'answer gate needs triage: {len(rows)} rows, {counts}')
checked=list(csv.DictReader((out/'status-aware-answers.csv').open()))
if len(checked)!=500:
    raise SystemExit('status-aware replay has incomplete coverage')
for row in checked:
    timeout_only=(row['outcome']=='INCOMPLETE' and
                  'timeout' in (row['ours'],row['reference']) and
                  row['ours'] in ('sat','unsat','timeout') and
                  row['reference'] in ('sat','unsat','timeout') and
                  row['ours_exit'] in ('0','124') and row['reference_exit'] in ('0','124'))
    if row['outcome']!='PASS' and not timeout_only:
        raise SystemExit(f'status-aware answer needs triage: {row}')
sat=[pathlib.Path(row['file']) for row in checked if row['ours']=='sat' and row['outcome']=='PASS']
if not sat:
    raise SystemExit('no successful SAT answers to replay')
(out/'sat-manifest.txt').write_text(''.join(str(path)+'\n' for path in sat))
identity={'parent_sha256':hashlib.sha256(summary.read_bytes()).hexdigest(),
          'status_aware_parent_sha256':hashlib.sha256((out/'status-aware-answers.csv').read_bytes()).hexdigest(),
          'answer_outcomes':counts,'expected_sat':len(sat),
          'validator_logic':'QF_NRA semantic superset; candidate and assertions unchanged',
          'input_hashes':{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in sat}}
(out/'model-identity.json').write_text(json.dumps(identity,indent=2)+'\n')
SELECT
  python3 scripts/validate_model.py --solver build-slurm-rel/primo \
    --solver-arg=--quiet --solver-arg=--check-model --expect-sat \
    --require-dolmen-pass --dolmen-arg=--strict=false \
    --dolmen-arg=--force-smtlib2-logic=QF_NRA \
    --timeout 30 --dolmen-timeout 30 --manifest "$out/sat-manifest.txt" \
    --out "$out/models"
elif (( task < 10 )); then
  python3 scripts/qf_lra_boolean_arithmetic_fuzz.py \
    --solver build-slurm-rel/primo --solver-arg=--quiet \
    --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
    --count 250 --seed "$seed" --timeout 5 --dolmen-timeout 10 \
    --out "$out/boolean-arithmetic"
else
  echo "invalid task $task" >&2
  exit 2
fi
