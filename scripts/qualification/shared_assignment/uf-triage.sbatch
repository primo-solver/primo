#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-uf-triage
#SBATCH --array=10-16%4
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
[[ -n ${SLURM_JOB_ID:-} ]]
python3 -u - <<'PY'
from pathlib import Path
import collections,hashlib,json,os,subprocess
root=Path(os.environ['PRIMO_GATE_OUT'])/('task-'+os.environ['SLURM_ARRAY_TASK_ID'])
out=Path(os.environ['PRIMO_UF_TRIAGE_OUT'])/root.name;out.mkdir(parents=True,exist_ok=False)
counts=collections.Counter();unresolved=[]
with (out/'rows.jsonl').open('w') as stream:
 for line in (root/'classified.jsonl').read_text().splitlines():
  row=json.loads(line);candidate=row['candidate'][0];reference=row['reference'][0];row['input_validator']=None;row['confirmation']=None
  assert hashlib.sha256(Path(row['file']).read_bytes()).hexdigest()==row['sha256']
  if row['outcome']=='PASS':outcome='answer-pass'
  else:
   cmd=['contrib/local/bin/dolmen','--strict=false','--size=1G',row['file']]
   try:
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=10)
    row['input_validator']={'command':cmd,'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
    if p.returncode!=0:outcome='input-rejected-by-dolmen'
    elif candidate in ('CRASH','UNKNOWN_OUTPUT','UNSUPPORTED_INPUT','MISSING'):outcome='valid-input-solver-error'
    elif candidate in ('sat','unsat') and reference in ('sat','unsat') and candidate!=reference:outcome='wrong-answer'
    elif candidate in ('sat','unsat'):
     cmd=['contrib/local/bin/cvc5','--enum-inst',row['file']]
     try:
      q=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
      row['confirmation']={'command':cmd,'exit':q.returncode,'stdout':q.stdout,'stderr':q.stderr}
      answer=q.stdout.strip()
      outcome='answer-confirmed-cvc5' if q.returncode==0 and answer==candidate else 'unconfirmed-answer'
      if q.returncode==0 and answer in ('sat','unsat') and answer!=candidate:outcome='wrong-answer'
     except subprocess.TimeoutExpired:outcome='unconfirmed-answer'
    else:outcome='solver-'+candidate.lower()
   except subprocess.TimeoutExpired:outcome='input-validation-timeout'
  row['classification']=outcome;counts[outcome]+=1
  if outcome in ('wrong-answer','valid-input-solver-error','unconfirmed-answer','input-validation-timeout'):unresolved.append(row['file'])
  stream.write(json.dumps(row)+'\n');stream.flush()
result={'counts':dict(counts),'unresolved':unresolved}
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
raise SystemExit(bool(unresolved))
PY
