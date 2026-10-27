#!/usr/bin/env bash
# Validate every ground SAT answer from the extra shared-driver profiles.
#SBATCH --job-name=primo-integer-shared-models
#SBATCH --array=0-5%6
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=01:00:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || exit 2
cd "${PRIMO_WORKDIR:?}"
tasks=(0 1 2 3 6 7)
task=${tasks[${SLURM_ARRAY_TASK_ID:?}]}
source="${PRIMO_GATE_OUT:?}/task-$task"
out="${PRIMO_MODEL_OUT:?}/task-$task"
mkdir -p "$out"
flags=(--solver-arg=--quiet --solver-arg=--preprocessing-mode=off)
case $task in
  0|6) flags+=(--solver-arg=--euf-prop=online);;
  1) flags+=(--solver-arg=--euf-engine=lazy --solver-arg=--euf-prop=off);;
  7) flags+=(--solver-arg=--no-qf-uflia-online-components);;
esac
python3 - "$source" "$out" "${PRIMO_EXPECTED_REVISION:?}" <<'PY'
import csv, hashlib, json, subprocess, sys
from pathlib import Path
source, out = map(Path, sys.argv[1:3])
metadata = json.loads((source / 'provenance.json').read_text())
binary = Path('build-slurm-rel/primo')
assert hashlib.sha256(binary.read_bytes()).hexdigest() == metadata['binary_sha256']
version = subprocess.check_output([str(binary), '--version'], text=True)
assert 'git: ' + sys.argv[3] in version.splitlines()
# The source runners classify nonzero solver exits before parsing answers.
rows = list(csv.DictReader((source / 'answers/summary.csv').open()))
paths = [Path(row.get('case') or row['file']).resolve()
         for row in rows if row['ours'] == 'sat']
assert len(paths) == len(set(paths))
(out / 'sat.txt').write_text(''.join(str(p) + '\n' for p in paths))
(out / 'source.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
if [[ -s $out/sat.txt ]]; then
  python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" \
    --expect-sat --require-dolmen-pass --dolmen contrib/local/bin/dolmen \
    --dolmen-arg=--strict=false --timeout 30 --dolmen-timeout 30 \
    --manifest "$out/sat.txt" --out "$out/models"
fi
