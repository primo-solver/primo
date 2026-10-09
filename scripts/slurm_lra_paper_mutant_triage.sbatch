#!/usr/bin/env bash
# Classify the dated campaign's candidate rejections with concrete Yices answers.
# Preserve baseline/candidate diagnostics and independent input-grammar checks.
#SBATCH --job-name=primo-lra-mutant-triage
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
python3 - <<'PY'
import collections,csv,json,pathlib,subprocess
base=pathlib.Path('results/lra-reverification-20261009')
out=base/'mutant-triage'
out.mkdir(exist_ok=True)
cases=[]
counts=collections.Counter()
for summary in sorted((base/'mutant-replay').glob('root-*/shard-*/answers.csv')):
    for row in csv.DictReader(summary.open()):
        if row['outcome']=='PASS':
            continue
        counts[(row['ours'],row['reference'])]+=1
        if row['reference'] in ('sat','unsat') and row['ours'] not in ('sat','unsat','timeout'):
            cases.append(row)
for index,row in enumerate(cases):
    path=pathlib.Path(row['file']).resolve()
    record={'original_row':row,'input':str(path)}
    (out/(str(index)+'.smt2')).symlink_to(path)
    for name,args in [('both',['build-slurm-rel/primo','--quiet','--lra-model-phase','--monotone-elimination']),('default',['build-slurm-rel/primo','--quiet']),('z3',['contrib/local/bin/z3']),('opensmt',['contrib/local/bin/opensmt']),('dolmen',['contrib/local/bin/dolmen','--strict=false'])]:
        try:
            proc=subprocess.run([*args,str(path)],text=True,capture_output=True,timeout=10)
            record[name]={'exit':proc.returncode,'stdout':proc.stdout,'stderr':proc.stderr}
        except subprocess.TimeoutExpired:
            record[name]={'timeout':True}
    (out/(str(index)+'.json')).write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record),flush=True)
(out/'summary.json').write_text(json.dumps({'concrete_reference_rejections':len(cases),'counts':{'/'.join(k):v for k,v in counts.items()}},indent=2)+'\n')
PY
