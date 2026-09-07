#!/usr/bin/env bash
# Frozen-model acceptance matrix: four preprocessing profiles per generator.
#SBATCH --job-name=primo-typed-cross-logic
#SBATCH --output=slurm_logs/primo-typed-cross-logic-%A_%a.out
#SBATCH --error=slurm_logs/primo-typed-cross-logic-%A_%a.err
#SBATCH --array=0-23%4
#SBATCH --time=00:30:00
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G

set -euo pipefail
cd "${PRIMO_WORKDIR:?set PRIMO_WORKDIR}"
export PATH="$PWD/contrib/local/bin:$PATH"
TASK=${SLURM_ARRAY_TASK_ID:-0}
COUNT=${PRIMO_FUZZ_COUNT:-750}
EXPECTED_GIT=${PRIMO_EXPECTED_GIT:?set PRIMO_EXPECTED_GIT}
if (( TASK < 0 || TASK > 23 )); then exit 2; fi
modes=(off basic strong auto)
families=(QF_UF QF_LIA QF_LRA QF_UFLIA QF_UFLRA QF_UF_Bool)
MODE=${modes[TASK % 4]}
FAMILY=${families[TASK / 4]}
SEED=$((202609400 + TASK))
OUT="${PRIMO_FUZZ_OUT_DIR:?set PRIMO_FUZZ_OUT_DIR}/task-$TASK"
mkdir -p "$OUT"
./build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: $EXPECTED_GIT" "$OUT/version.txt"
z3 --version > "$OUT/reference-version.txt"
solver_args=(--solver-arg=--quiet --solver-arg=--check-model --solver-arg=--preprocessing-mode="$MODE")
extra_options=()
if [[ -n ${PRIMO_FUZZ_SOLVER_ARGS:-} ]]; then
  read -r -a extra_options <<< "$PRIMO_FUZZ_SOLVER_ARGS"
  for arg in "${extra_options[@]}"; do solver_args+=(--solver-arg="$arg"); done
fi
printf '%s\n' "$FAMILY" "$MODE" "$SEED" "${solver_args[@]}" > "$OUT/profile.txt"

case "$FAMILY" in
  QF_UF|QF_LIA|QF_LRA)
    python3 scripts/fuzz_external.py --fuzzer fuzzsmt \
      --solver "./build-slurm-rel/primo --quiet --check-model --preprocessing-mode=$MODE ${extra_options[*]}" \
      --refs z3 --logic "$FAMILY" --count "$COUNT" --timeout 5 --seed "$SEED" \
      --fuzzer-args '-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4' \
      --out "$OUT/answer"
    cases=("$OUT"/answer/generated/*.smt2)
    ;;
  QF_UFLIA|QF_UFLRA)
    generator=scripts/qf_uflia_fuzz.py
    if [[ $FAMILY == QF_UFLRA ]]; then generator=scripts/qf_uflra_smoke_fuzz.py; fi
    python3 "$generator" --solver ./build-slurm-rel/primo "${solver_args[@]}" \
      --ref z3 --count "$COUNT" --seed "$SEED" --shape mixed --timeout 5 --out "$OUT/answer"
    cases=("$OUT"/answer/cases/*.smt2)
    ;;
  QF_UF_Bool)
    python3 scripts/qf_uf_boolean_fuzz.py --solver ./build-slurm-rel/primo \
      "${solver_args[@]}" --ref contrib/local/bin/z3 --count "$COUNT" --seed "$SEED" \
      --shape all --keep-passing --timeout 5 --out "$OUT/answer"
    cases=("$OUT"/answer/cases/*.smt2)
    ;;
esac

model_status=0
python3 scripts/validate_model.py --solver ./build-slurm-rel/primo "${solver_args[@]}" \
  --require-dolmen-pass --dolmen-arg=--strict=false --timeout 5 --dolmen-timeout 5 \
  --out "$OUT/models" "${cases[@]}" || model_status=$?

python3 - "$OUT" "$COUNT" "$model_status" <<'PY'
import csv
import json
import os
from collections import Counter
from pathlib import Path
import socket
import sys

root = Path(sys.argv[1])
count, status = map(int, sys.argv[2:])
summary = {"count": count, "host": socket.gethostname(),
           "job_id": os.environ.get("SLURM_JOB_ID"), "model_exit": status,
           "profile": (root / "profile.txt").read_text().splitlines()}
allowed = {
    "answer": {"PASS", "UNKNOWN_INVOLVED", "TIMEOUT"},
    "models": {"pass", "not-sat", "solver-unknown", "solver-timeout", "dolmen-timeout"},
}
failed = status not in (0, 1)
for kind, outcomes in allowed.items():
    with (root / kind / "summary.csv").open() as source:
        rows = list(csv.DictReader(source))
    counts = Counter(row["outcome"] for row in rows)
    summary[kind] = dict(counts)
    failed |= len(rows) != count or bool(set(counts) - outcomes)
unresolved = sum(summary["models"].get(key, 0) for key in
                 ("solver-unknown", "solver-timeout", "dolmen-timeout"))
failed |= status == 1 and unresolved == 0
summary["accepted"] = not failed
(root / "gate-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, sort_keys=True))
sys.exit(1 if failed else 0)
PY
