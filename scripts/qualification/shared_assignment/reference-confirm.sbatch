#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-confirm
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:02:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
from pathlib import Path
import hashlib,json,os,subprocess
p=Path('tmp/shared-assignment-data/comparison/UF/20201221-induction-by-reflection-schoisswohl/20201221-induction-by-reflection-schoisswohl_universalInstance.smt2')
command=['contrib/local/bin/cvc5','--enum-inst',str(p)]
r=subprocess.run(command,capture_output=True,text=True,timeout=60)
out=Path(os.environ['PRIMO_REFERENCE_OUT'])/'universal-instance-confirmation.json'
out.write_text(json.dumps({'command':command,'timeout':60,'job_id':os.environ['SLURM_JOB_ID'],'input_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr},indent=2)+'\n')
print(r.stdout,r.stderr)
raise SystemExit(r.returncode!=0 or r.stdout.strip()!='unsat')
PY
