#!/usr/bin/env bash
# Aggregate afterany retained-mutant replay: only exact agreements and clearly
# identified resource-only timeouts qualify. Every other outcome blocks timing.
#SBATCH --job-name=primo-lra-default-qualify
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 - <<'QUALIFY'
import collections,csv,hashlib,json,os,pathlib
base=pathlib.Path(os.environ['PRIMO_CAMPAIGN_OUT'])
source=pathlib.Path(os.environ['PRIMO_MUTANT_ROOT'])
corpus=pathlib.Path(os.environ['PRIMO_CORPUS_ROOT'])
expected_git=os.environ['PRIMO_EXPECTED_GIT']
expected_sha=os.environ['PRIMO_EXPECTED_SHA256']
files={path.resolve():hashlib.sha256(path.read_bytes()).hexdigest() for path in source.rglob('*.smt2')}
families={path.name for path in corpus.iterdir() if path.is_dir()}
covered={family for family in families if any(path.name.startswith(family+'_') for path in files)}
if not files or covered!=families:
    raise SystemExit(f'incomplete family coverage: {covered} of {families}')
seen={}
counts=collections.Counter()
timeouts=[]
for shard in range(16):
    root=base/f'shard-{shard}'
    if f'git: {expected_git}' not in (root/'version.txt').read_text().splitlines():
        raise SystemExit('replay binary revision changed')
    if (root/'binary.sha256').read_text().split()[0]!=expected_sha:
        raise SystemExit('replay binary identity changed')
    provenance=json.loads((root/'provenance.json').read_text())
    if provenance['total_mutants']!=len(files):
        raise SystemExit('retained corpus changed during replay')
    selected={pathlib.Path(path).resolve():digest for path,digest in provenance['inputs'].items()}
    rows=list(csv.DictReader((root/'answers.csv').open()))
    if len(rows)!=len(selected):
        raise SystemExit('incomplete replay shard')
    for row in rows:
        path=pathlib.Path(row['file']).resolve()
        if path in seen or path not in selected or files.get(path)!=selected[path]:
            raise SystemExit('duplicate, changed or unmatched input')
        seen[path]=selected[path]
        agreement=(row['outcome']=='PASS' and row['ours']==row['reference'] and
                   row['ours'] in ('sat','unsat') and row['ours_exit']==row['reference_exit']=='0')
        timeout_only=(row['outcome']=='INCOMPLETE' and
                      'timeout' in (row['ours'],row['reference']) and
                      row['ours'] in ('sat','unsat','timeout') and
                      row['reference'] in ('sat','unsat','timeout') and
                      row['ours_exit'] in ('0','124') and row['reference_exit'] in ('0','124'))
        if agreement:
            counts['pass']+=1
        elif timeout_only:
            counts['timeout']+=1
            timeouts.append(row)
        else:
            raise SystemExit(f'untriaged non-resource outcome: {row}')
if seen!=files:
    raise SystemExit('retained-input coverage differs from the original campaign')
bugs=source.parent/'bugs'
if bugs.exists() and any(path.is_file() for path in bugs.rglob('*')):
    raise SystemExit('raw mutation bug artifacts need explicit triage')
report={'status':'qualified','git':expected_git,'binary_sha256':expected_sha,
        'rows':len(files),'families':sorted(covered),'counts':dict(counts),
        'timeout_rows':timeouts,'summary':'Every retained mutant independently classified; resource-only timeouts are not passes.'}
(base/'qualification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({key:value for key,value in report.items() if key!='timeout_rows'},indent=2))
QUALIFY
