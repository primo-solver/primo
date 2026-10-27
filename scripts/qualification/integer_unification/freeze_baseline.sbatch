#!/usr/bin/env bash
#SBATCH --job-name=primo-integer-freeze
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
mkdir -p results/integer-baseline
build-slurm-rel/primo --version > results/integer-baseline/version.txt
sha256sum build-slurm-rel/primo > results/integer-baseline/binary.sha256
python3 - <<'PY'
import hashlib, json, os
from pathlib import Path
root=Path(os.environ['PRIMO_CORPUS_ROOT'])
out=Path('results/integer-baseline')
for logic in ('QF_LIA','QF_UFLIA'):
    rows=[]
    for path in sorted((root/logic).rglob('*.smt2')):
        rows.append({'path':str(path.relative_to(root/logic)), 'sha256':hashlib.sha256(path.read_bytes()).hexdigest(), 'bytes':path.stat().st_size})
    assert rows, logic
    (out/(logic+'.json')).write_text(json.dumps(rows,indent=2)+'\n')
    (out/(logic+'.txt')).write_text(''.join(row['path']+'\n' for row in rows))
    print(logic,len(rows),sum(row['bytes'] for row in rows),flush=True)
(out/'resources.json').write_text(json.dumps({'timeout_seconds':60,'memory_gib':8,'cpus':1,'paired_rounds':2,'baseline_revision':'8ca8ebe8db75966fc8aefdd928a0878e648e7f10'},indent=2)+'\n')
PY
