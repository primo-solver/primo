#!/usr/bin/env bash
# Dated paper-profile correctness replay, complementing the strict QF_LRA run.
# Keep assertions and the candidate's logic unchanged. Dolmen alone checks
# the printed model in QF_NRA, a semantic superset accepting negative constant
# divisors. These passes do not certify strict QF_LRA input conformance.
#SBATCH --job-name=primo-lra-fuzz-semantic-replay
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
task=${SLURM_ARRAY_TASK_ID:-0}
out=results/lra-reverification-20261009/model-fuzz/semantic-sat-replay/shard-$task
mkdir -p "$out"
python3 - "$task" "$out" <<'SELECT'
import pathlib,csv,sys,json,hashlib,collections
source=pathlib.Path('results/lra-reverification-20261009/model-fuzz/fuzzsmt')/('shard-'+sys.argv[1])
out=pathlib.Path(sys.argv[2])
rows=list(csv.DictReader((source/'summary.csv').open()))
assert len(rows)==500,len(rows)
assert all(x['outcome'] in ('PASS','TIMEOUT') for x in rows),dict(collections.Counter(x['outcome'] for x in rows))
sat=[pathlib.Path(x['file']) for x in rows if x['ours']=='sat' and x['outcome']=='PASS']
(out/'sat-manifest.txt').write_text(''.join(str(p)+'\n' for p in sat))
bin=pathlib.Path('build-slurm-rel/primo')
identity={'parent':str(source),'parent_sha256':hashlib.sha256((source/'summary.csv').read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(bin.read_bytes()).hexdigest(),'solver_args':['--quiet','--lra-model-phase','--monotone-elimination','--check-model'],'expected_sat':len(sat),'input_hashes':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sat}}
(out/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
print('selected SAT',len(sat),'from',len(rows),'answer passes')
SELECT
python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet \
  --solver-arg=--lra-model-phase --solver-arg=--monotone-elimination --solver-arg=--check-model \
  --expect-sat --require-dolmen-pass --dolmen-arg=--strict=false --dolmen-arg=--force-smtlib2-logic=QF_NRA \
  --timeout 10 --dolmen-timeout 10 --manifest "$out/sat-manifest.txt" --out "$out/models"
