#!/usr/bin/env bash
# Targeted merge gate for scaled/summed constant ITE arena-lifetime repairs.
# Use the workspace preparation helper and its --check-only before submission.
#SBATCH --job-name=primo-cite-hoisting
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail

cd "${PRIMO_WORKDIR:?prepared workspace required}"
OUT=${PRIMO_FUZZ_OUT_DIR:?fresh artifact directory required}
EXPECTED_GIT=${PRIMO_EXPECTED_GIT:?expected commit required}
export PATH="$PWD/contrib/local/bin:$PATH"
mkdir -p "$(dirname "$OUT")"
./build-slurm-rel/primo --version | tee "$OUT-version.txt"
./build-slurm-rel/primo --version | grep -F "$EXPECTED_GIT"
z3 --version

# Original grammar: constant leaves, Boolean ITEs, literal scales, and sums.
# Separate arithmetic sorts, mixed branch outcomes, nested shapes and guard
# assignments exercise hoisting without involving UF model completion.
python3 - "$OUT" <<'PY'
from pathlib import Path
import random
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=False)
rng = random.Random(202609070)
def numeral(value):
    return str(value) if value >= 0 else f"(- {-value})"

def term(depth):
    if depth == 0:
        return numeral(rng.randint(-9, 9))
    left, right = term(depth - 1), term(depth - 1)
    guard = rng.choice(("p", "q", "r"))
    branch = f"(ite {guard} {left} {right})"
    if rng.choice((False, True)):
        return f"(* {numeral(rng.choice((-3, -2, 2, 3)))} {branch})"
    return f"(+ {numeral(rng.randint(-5, 5))} {branch})"

for index in range(1000):
    logic = "QF_LIA" if index % 2 == 0 else "QF_LRA"
    declarations = "\n".join(f"(declare-fun {name} () Bool)" for name in ("p", "q", "r"))
    expression = term(1 + index % 3)
    comparison = rng.choice(("<=", "<", "="))
    constant = numeral(rng.randint(-15, 15))
    lhs, rhs = (expression, constant) if index % 4 < 2 else (constant, expression)
    pins = "\n".join(f"(assert {name if rng.choice((False, True)) else f'(not {name})'})"
                     for name in ("p", "q", "r") if rng.choice((False, True)))
    (out / f"case-{index:04d}.smt2").write_text(
        f"(set-logic {logic})\n{declarations}\n{pins}\n"
        f"(assert ({comparison} {lhs} {rhs}))\n(check-sat)\n")
PY

python3 scripts/compare_with_z3.py "$OUT"/*.smt2 \
  --solver ./build-slurm-rel/primo --refs z3 --timeout 5 --out "$OUT/summary.csv"
python3 scripts/compare_with_z3.py benchmarks/regressions/qf_lia_scaled_ite_arena_growth.smt2 \
  --solver ./build-slurm-rel/primo --refs z3 --timeout 5 --out "$OUT/regression.csv"
