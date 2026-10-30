#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-case-study
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --hint=nomultithread
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PYTHONPATH="$PWD/scripts"
artifacts="${PRIMO_CASE_ARTIFACTS:?}"
results="${PRIMO_CASE_RESULTS:?}"
corpus="${PRIMO_CASE_CORPUS:?}"
case "${PRIMO_CASE_PHASE:?}" in
 collect)
  python3 "$artifacts/prepare_lra_case_study.py" collect \
   --extraction "$results/extract" \
   --source-manifest "$artifacts/qf-lra-case-study-sources.txt" \
   --include "$artifacts/existing/benchmarks/qf-lra-call-pilot" \
   --include "$artifacts/existing/benchmarks/qf-lra-captured-calls" \
   --out "$results/pool"
  python3 "$artifacts/prepare_lra_case_study.py" select \
   --pool "$results/pool" --out "$corpus" --target 1000 \
   --seed 20261030 --max-per-parent 25 --max-per-family 400
  ;;
 references|reference-models)
  labels=(yices-smt2 opensmt)
  index=${SLURM_ARRAY_TASK_ID:?}
  label=${labels[$((index / 32))]}
  shard=$((index % 32))
  target="$results/references/$label/shard-$shard"
  if [[ $PRIMO_CASE_PHASE == reference-models ]]; then
   export PRIMO_CASE_REFERENCE_TARGET="$target"
   python3 - <<'PY'
import csv
import os
from pathlib import Path
target = Path(os.environ['PRIMO_CASE_REFERENCE_TARGET'])
root = Path(os.environ['PRIMO_CASE_CORPUS'])
with (target / 'answers.csv').open() as stream:
    rows = list(csv.DictReader(stream))
paths = [str(root / row['file']) for row in rows
         if (row['status'], row['answer'], row['exit_code'], row['timeout'])
         == ('ok', 'sat', '0', 'false')]
(target / 'sat-manifest.txt').write_text(''.join(p + '\n' for p in paths))
PY
   if [[ -s $target/sat-manifest.txt ]]; then
    python3 scripts/validate_model.py --solver "$PWD/tmp/reference-panel/$label" \
     --manifest "$target/sat-manifest.txt" --out "$target/models" \
     --expect-sat --require-dolmen-pass --dolmen contrib/local/bin/dolmen \
     --dolmen-size 2G --dolmen-arg=--strict=false --timeout 90 --dolmen-timeout 30
   fi
   exit
  fi
  python3 scripts/benchmark_model_replay.py benchmark \
   --root "$corpus" --manifest "$corpus/manifest.txt" \
   --solver "$PWD/tmp/reference-panel/$label" \
   --timeout 15 --shards 32 --shard-index "$shard" \
   --run-id lra-case-study-1000-20261030 \
   --out "$target"
  export PRIMO_CASE_REFERENCE_TARGET="$target"
  python3 - <<'PY'
import csv
import json
import os
from pathlib import Path
target = Path(os.environ['PRIMO_CASE_REFERENCE_TARGET'])
expected = json.loads((Path(os.environ['PRIMO_CASE_CORPUS']) / 'expected-statuses.json').read_text())
with (target / 'answers.csv').open() as stream:
    rows = list(csv.DictReader(stream))
for row in rows:
    assert row['status'] in ('ok', 'timeout'), 'reference error requires triage'
    assert row['wrong'] == 'false'
    known = expected[row['file']]
    if row['status'] == 'ok' and row['answer'] in ('sat', 'unsat') and known != 'unknown':
        assert row['answer'] == known, 'reference disagreement requires triage'
PY
  ;;
 *) exit 2 ;;
esac
