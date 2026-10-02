#!/usr/bin/env bash
#SBATCH --job-name=primo-symmetry-repro
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:03:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo "SLURM allocation required" >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
root=results/symmetry-reproduction
mkdir -p "$root"
./build-slurm-rel/primo --version > "$root/version.txt"
sha256sum build-slurm-rel/primo tmp/symmetry/*.smt2 > "$root/sha256.txt"
python3 - <<'PY'
import csv, subprocess
from pathlib import Path
root=Path('results/symmetry-reproduction')
profiles={'default':[], 'basic-zero':['--preprocessing-mode=basic','--preprocessing-passes=0'], 'basic-zero-no-precedence':['--preprocessing-mode=basic','--preprocessing-passes=0','--no-finite-domain-value-precedence'], 'symmetry-off':['--preprocessing-mode=basic','--preprocessing-passes=0','--no-uf-symmetry-breaking']}
rows=[]
for path in sorted(Path('tmp/symmetry').glob('*.smt2')):
    for label,args in profiles.items():
        p=subprocess.run(['build-slurm-rel/primo','--quiet','--stats',*args,str(path)],capture_output=True,text=True,timeout=10)
        (root/f'{path.stem}-{label}.stdout').write_text(p.stdout)
        (root/f'{path.stem}-{label}.stderr').write_text(p.stderr)
        rows.append([path.name,label,p.returncode,p.stdout.strip()])
    for ref in ['z3','cvc5']:
        p=subprocess.run([f'contrib/local/bin/{ref}',str(path)],capture_output=True,text=True,timeout=10)
        rows.append([path.name,ref,p.returncode,p.stdout.strip()])
with (root/'results.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['input','profile','exit','answer']);w.writerows(rows)
for row in rows:print(*row,sep=' | ')
if any(row[2] != 0 for row in rows):raise SystemExit(1)
PY
