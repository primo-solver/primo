#!/usr/bin/env bash
# Prepare/preflight a clean revision before submitting; reuse recorded references.
#SBATCH --job-name=primo-online-parser
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?prepared workspace}"
OUT=${PRIMO_ONLINE_OUT:?fresh output directory}
export PATH="$PWD/contrib/local/bin:$PATH"
mkdir "$OUT"
build-slurm-rel/primo --version | tee "$OUT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_REVISION:?expected revision}" "$OUT/version.txt"
sha256sum build-slurm-rel/primo > "$OUT/binary-before.sha256"
python3 scripts/check_smtlib_lexical_conformance.py --primo build-slurm-rel/primo \
  --policy --mutations 1000 --seed 20260910 --only primo \
  --source-commit "$PRIMO_EXPECTED_REVISION" --out "$OUT/policy"
python3 -I - "$OUT/policy/results.json" "${PRIMO_RECORDED_REFERENCES:?recorded results.json}" <<'PY' > "$OUT/reference-replay.json"
import json
import sys
from collections import defaultdict
from pathlib import Path

candidate = json.loads(Path(sys.argv[1]).read_text())
references = defaultdict(list)
for row in json.loads(Path(sys.argv[2]).read_text()):
    if row["profile"] in {"z3", "cvc5"}:
        references[row["case"]].append(row)
generated = [row for row in candidate if row["case"].startswith("mutation_")]
assert len(generated) == 1000
assert len({row["case"] for row in generated}) == 1000
for row in generated:
    saved = references[row["case"]]
    assert row["outcome"] == "answer-pass" and row["returncode"] == 0
    assert len(saved) == 2 and {ref["profile"] for ref in saved} == {"z3", "cvc5"}
    for ref in saved:
        assert ref["outcome"] == "answer-pass" and ref["returncode"] == 0
        assert ref["input_sha256"] == row["input_sha256"]
        assert ref["answers"] == row["answers"]
print(json.dumps({"matched_sessions": 1000, "matched_reference_results": 2000, "failures": 0}))
PY
for seed in {0..99}; do
  python3 tests/interactive_session.py build-slurm-rel/primo "$seed"
  printf '%s\n' "$seed" >> "$OUT/live-completed.txt"
done
python3 scripts/validate_model.py --solver build-slurm-rel/primo \
  --solver-arg=--quiet --solver-arg=--check-model --solver-arg=--max-theory-conflicts=1000 \
  --expect-sat --require-dolmen-pass --out "$OUT/model" benchmarks/smoke/quoted_names_model.smt2
sha256sum --check "$OUT/binary-before.sha256"
