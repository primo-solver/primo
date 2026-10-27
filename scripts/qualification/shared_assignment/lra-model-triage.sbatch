#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-lra-model
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=00:10:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 -u - <<'PY'
from pathlib import Path
import collections,csv,hashlib,json,os,subprocess
root=Path(os.environ['PRIMO_COMPARE_OUT_ROOT'])
out=root/'lra-model-triage';out.mkdir(exist_ok=False)
rows=[]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for summary in sorted(root.glob('*/round-*/candidate/shard-*/full-coverage/models/summary.csv')):
 for index,row in enumerate(csv.DictReader(summary.open())):
  if row['outcome']=='pass':continue
  if row['outcome']!='input-nonconforming' or '/QF_LRA/' not in row['file']:
   raise RuntimeError('unexpected model outcome: '+str(row))
  prefix=summary.parent/f'{index:05d}-{Path(row["file"]).name}'
  problem=Path(str(prefix)+'.smt2');model=Path(str(prefix)+'.rsmt2')
  stderr=Path(str(prefix)+'.dolmen.stderr').read_text()
  assert 'Forbidden expression in linear arithmetic' in stderr
  command=['contrib/local/bin/dolmen','--check-model=true','-r',str(model),'--strict=false','--force-smtlib2-logic=QF_NRA','--size=2G',str(problem)]
  proc=subprocess.run(command,capture_output=True,text=True,timeout=30)
  rows.append({'parent':str(summary),'file':row['file'],'input_sha256':digest(problem),'model_sha256':digest(model),'command':command,'exit':proc.returncode,'stdout':proc.stdout,'stderr':proc.stderr,'classification':'model-pass-with-wider-logic' if proc.returncode==0 else 'unresolved'})
  (out/'rows.json').write_text(json.dumps(rows,indent=2)+'\n')
counts=dict(collections.Counter(r['classification'] for r in rows))
(out/'summary.json').write_text(json.dumps({'counts':counts,'note':'Same saved assertions and model; validator forced to QF_NRA for constant-denominator syntax. These are not direct passes under the input logic.'},indent=2)+'\n')
print(counts)
raise SystemExit(not rows or any(r['exit']!=0 for r in rows))
PY
