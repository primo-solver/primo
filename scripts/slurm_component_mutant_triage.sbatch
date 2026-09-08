#!/usr/bin/env bash
# Input-only independent classification of a rejected mutation attempt.
#SBATCH --job-name=primo-mutant-triage
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 - <<'PY'
import csv
import hashlib
import json
import os
import subprocess
from pathlib import Path
root = Path(os.environ["PRIMO_VALIDATION_DIR"])
out = Path(os.environ["PRIMO_TRIAGE_OUT"])
out.mkdir(parents=True, exist_ok=True)
rows = list(csv.DictReader((root / "summary.csv").open()))
results = []
for index, row in enumerate(rows):
    if row["outcome"] not in ("solver-error", "input-nonconforming"):
        continue
    source = Path(row["file"])
    command = ["contrib/local/bin/dolmen", "--strict=true", "--size=1G", str(source)]
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=10)
        item = dict(exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
    except subprocess.TimeoutExpired:
        item = dict(exit=None, stdout="", stderr="input-only timeout")
    item.update(file=str(source), command=command, sha256=hashlib.sha256(source.read_bytes()).hexdigest())
    (out / f"case-{index:05d}.json").write_text(json.dumps(item, indent=2))
    results.append(item)
    print(index, item["exit"], source.name, flush=True)
(out / "summary.json").write_text(json.dumps(results, indent=2))
PY
