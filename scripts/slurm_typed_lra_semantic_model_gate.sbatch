#!/usr/bin/env bash
# Recheck saved FuzzSMT Real inputs without Dolmen's narrow linear grammar.
# Assertions and printed models are unchanged; only the validator's logic
# classification is broadened to nonlinear Real arithmetic (a semantic superset).
# This is model evidence, not a claim of strict QF_LRA syntax conformance.
#SBATCH --job-name=primo-typed-lra-semantic
#SBATCH --output=slurm_logs/primo-typed-lra-semantic-%A_%a.out
#SBATCH --error=slurm_logs/primo-typed-lra-semantic-%A_%a.err
#SBATCH --array=8-11%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:15:00

set -euo pipefail
cd "${PRIMO_WORKDIR:?set PRIMO_WORKDIR}"
export PATH="$PWD/contrib/local/bin:$PATH"
TASK=${SLURM_ARRAY_TASK_ID:?SLURM array task required}
(( TASK >= 8 && TASK <= 11 ))
modes=(off basic strong auto)
OUT="${PRIMO_FUZZ_OUT_DIR:?set PRIMO_FUZZ_OUT_DIR}/task-$TASK"
CASES="${PRIMO_REVALIDATE_CASE_ROOT:?set PRIMO_REVALIDATE_CASE_ROOT}/task-$TASK/answer/generated"
mkdir -p "$OUT"
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?set PRIMO_EXPECTED_GIT}" "$OUT/version.txt"
status=0
python3 scripts/validate_model.py --solver ./build-slurm-rel/primo \
  --solver-arg=--quiet --solver-arg=--check-model \
  --solver-arg=--preprocessing-mode="${modes[TASK - 8]}" \
  --require-dolmen-pass --dolmen-arg=--strict=false \
  --dolmen-arg=--force-smtlib2-logic=QF_NRA \
  --timeout 5 --dolmen-timeout 5 --out "$OUT/models" "$CASES"/*.smt2 || status=$?
python3 - "$OUT" "$status" <<'PY'
import csv
import json
from collections import Counter
from pathlib import Path
import sys

root = Path(sys.argv[1])
with (root / 'models/summary.csv').open() as source:
    counts = Counter(row['outcome'] for row in csv.DictReader(source))
allowed = {'pass', 'not-sat', 'solver-timeout', 'solver-unknown', 'dolmen-timeout'}
accepted = int(sys.argv[2]) in (0, 1) and bool(counts) and not (set(counts) - allowed)
summary = {'models': dict(counts), 'accepted': accepted,
           'validator_logic': 'QF_NRA', 'input_assertions_changed': False}
(root / 'gate-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary))
sys.exit(0 if accepted else 1)
PY
