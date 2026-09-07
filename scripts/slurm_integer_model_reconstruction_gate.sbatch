#!/usr/bin/env bash
# QF_UFLIA reconstruction gate on the two seed ranges that exposed this bug.
# Source and dependencies must be prepared by slurm_prepare_benchmark_workspace.py.
# Unknown/timeout rows remain explicit; only actual errors fail the gate.
#SBATCH --job-name=primo-int-model-gate
#SBATCH --output=slurm_logs/primo-int-model-%A_%a.out
#SBATCH --error=slurm_logs/primo-int-model-%A_%a.err
#SBATCH --array=0-7%4
#SBATCH --time=00:15:00
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G

set -euo pipefail
cd "${PRIMO_WORKDIR:?set PRIMO_WORKDIR to the prepared workspace}"
export PATH="$PWD/contrib/local/bin:$PATH"
EXPECTED_GIT=${PRIMO_EXPECTED_GIT:?set PRIMO_EXPECTED_GIT}
COUNT=${PRIMO_FUZZ_COUNT:-750}
TASK=${SLURM_ARRAY_TASK_ID:-0}
if (( TASK < 0 || TASK > 7 )); then
  echo "expected array task 0 through 7" >&2
  exit 2
fi
if (( TASK < 4 )); then
  SEED=$((202609100 + TASK))
else
  SEED=$((202609200 + TASK - 4))
fi
OUT="${PRIMO_FUZZ_OUT_DIR:?set PRIMO_FUZZ_OUT_DIR}/task-$TASK"
mkdir -p "$OUT"
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: $EXPECTED_GIT" "$OUT/version.txt"

# The eight witness files are small enough to run in every shard, before fuzzing.
python3 scripts/validate_model.py --solver ./build-slurm-rel/primo \
  --require-dolmen-pass --dolmen-arg=--strict=false --timeout 5 --dolmen-timeout 5 \
  --out "$OUT/witnesses" \
  bugs/qf-uflia-nested-application-model/witness*.smt2 \
  benchmarks/regressions/qf_uflia_model_eliminated_definition.smt2

python3 scripts/qf_uflia_fuzz.py --solver ./build-slurm-rel/primo \
  --solver-arg=--quiet --ref z3 --count "$COUNT" --seed "$SEED" \
  --shape mixed --depth 3 --timeout 5 --out "$OUT/answer"

model_status=0
python3 scripts/validate_model.py --solver ./build-slurm-rel/primo \
  --solver-arg=--quiet --require-dolmen-pass --dolmen-arg=--strict=false \
  --timeout 5 --dolmen-timeout 5 --out "$OUT/models" \
  "$OUT"/answer/cases/*.smt2 || model_status=$?

python3 - "$OUT" "$COUNT" "$SEED" "$model_status" <<'PY'
import csv
import json
import os
from collections import Counter
from pathlib import Path
import socket
import sys

root, count, seed, model_status = Path(sys.argv[1]), *map(int, sys.argv[2:])
summary = {"count": count, "seed": seed, "host": socket.gethostname(),
           "job_id": os.environ.get("SLURM_JOB_ID"), "model_exit": model_status}
allowed = {
    "answer": {"PASS", "UNKNOWN_INVOLVED", "TIMEOUT"},
    "models": {"pass", "not-sat", "solver-unknown", "solver-timeout", "dolmen-timeout"},
}
failed = model_status not in (0, 1)
for kind, outcomes in allowed.items():
    with (root / kind / "summary.csv").open() as source:
        rows = list(csv.DictReader(source))
    counts = Counter(row["outcome"] for row in rows)
    summary[kind] = dict(counts)
    failed |= len(rows) != count or bool(set(counts) - outcomes)
# Exit 1 is legitimate only when the validator recorded an unresolved row.
unresolved = sum(summary["models"].get(key, 0) for key in
                 ("solver-unknown", "solver-timeout", "dolmen-timeout"))
failed |= model_status == 1 and unresolved == 0
summary["accepted"] = not failed
(root / "gate-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, sort_keys=True))
sys.exit(1 if failed else 0)
PY
