#!/usr/bin/env bash
# SLURM model-validation batch for sandbox/non-incremental/QF_LRA.
#
# Each array task filters its shard through Z3 first. Only Z3-sat files are
# passed to Primo's get-model validator. The task stops on a definite model
# validation error and leaves the offending artifacts in the shard directory.

#SBATCH --job-name=primo-qflra-sandbox-model
#SBATCH --array=0-7
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the remote workspace}
SOLVER_BIN=${PRIMO_MODEL_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
CORPUS=${PRIMO_MODEL_CORPUS:-"$WORKDIR/sandbox/non-incremental/QF_LRA"}
OUT_ROOT=${PRIMO_MODEL_OUT_ROOT:-"$WORKDIR/results/qf-lra-sandbox-model-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
TASK_COUNT=${SLURM_ARRAY_TASK_COUNT:-8}
Z3_BIN=${PRIMO_MODEL_Z3:-"$WORKDIR/contrib/local/bin/z3"}
VALIDATOR=${PRIMO_MODEL_VALIDATOR:-"$WORKDIR/scripts/validate_model.py"}
TIMEOUT=${PRIMO_MODEL_TIMEOUT:-30}
DOLMEN_TIMEOUT=${PRIMO_MODEL_DOLMEN_TIMEOUT:-30}
PERF_LIMIT=${PRIMO_MODEL_PERF_LIMIT:-24}
PERF_TIMEOUT=${PRIMO_MODEL_PERF_TIMEOUT:-60}
export WORKDIR SOLVER_BIN CORPUS OUT_ROOT TASK_ID TASK_COUNT Z3_BIN VALIDATOR
export TIMEOUT DOLMEN_TIMEOUT PERF_LIMIT PERF_TIMEOUT

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

PATH="$WORKDIR/contrib/local/bin:$PATH"
export PATH

"$SOLVER_BIN" --version
"$Z3_BIN" --version | head -1 || true

python3 - <<'PY'
import csv
import hashlib
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

workdir = Path(os.environ["WORKDIR"])
solver = os.environ["SOLVER_BIN"]
corpus = Path(os.environ["CORPUS"])
out_root = Path(os.environ["OUT_ROOT"])
task_id = int(os.environ["TASK_ID"])
task_count = int(os.environ["TASK_COUNT"])
z3 = os.environ["Z3_BIN"]
validator = os.environ["VALIDATOR"]
timeout = float(os.environ["TIMEOUT"])
dolmen_timeout = float(os.environ["DOLMEN_TIMEOUT"])
perf_limit = int(os.environ["PERF_LIMIT"])
perf_timeout = float(os.environ["PERF_TIMEOUT"])

shard = out_root / f"shard-{task_id}"
validation_root = shard / "model-validation"
shard.mkdir(parents=True, exist_ok=True)
validation_root.mkdir(parents=True, exist_ok=True)

files = sorted(path for path in corpus.rglob("*.smt2") if path.is_file())
files = [path for index, path in enumerate(files) if index % task_count == task_id]

def first_answer(stdout: str) -> str:
    for line in stdout.splitlines():
        stripped = line.strip()
        if stripped in {"sat", "unsat", "unknown"}:
            return stripped
    return ""

def run(command, input_text: Optional[str], limit: float):
    start = time.perf_counter()
    try:
        completed = subprocess.run(
            command,
            input=input_text,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=limit,
            check=False,
        )
        return completed.returncode, completed.stdout, completed.stderr, time.perf_counter() - start, False
    except subprocess.TimeoutExpired as exc:
        return None, exc.stdout or "", exc.stderr or "", time.perf_counter() - start, True

def model_problem_text(text: str) -> str:
    if ":produce-models" not in text:
        text = "(set-option :produce-models true)\n" + text
    if "(get-model" not in text and "( get-model" not in text:
        text = text.rstrip() + "\n(get-model)\n"
    return text

def safe_stem(index: int, path: Path) -> str:
    digest = hashlib.sha256(str(path).encode()).hexdigest()[:12]
    return f"{index:05d}-{digest}-{path.name}"

rows = []
perf_rows = []
sat_files = []
stop_reason = ""

for index, path in enumerate(files):
    stem = safe_stem(index, path)
    z3_rc, z3_out, z3_err, z3_seconds, z3_timeout = run([z3, "-smt2", str(path)], None, timeout)
    z3_answer = first_answer(z3_out)
    row = {
        "file": str(path),
        "z3_answer": z3_answer if not z3_timeout else "timeout",
        "z3_exit": "" if z3_rc is None else str(z3_rc),
        "z3_seconds": f"{z3_seconds:.6f}",
        "validation_outcome": "not-run",
        "validation_seconds": "",
        "message": "",
    }
    if z3_timeout or z3_rc != 0 or z3_answer != "sat":
        rows.append(row)
        continue

    sat_files.append(path)
    per_file_out = validation_root / stem
    cmd = [
        sys.executable,
        validator,
        "--solver",
        solver,
        "--solver-arg=--quiet",
        "--z3",
        z3,
        "--timeout",
        str(timeout),
        "--dolmen-timeout",
        str(dolmen_timeout),
        "--out",
        str(per_file_out),
        str(path),
    ]
    rc, out, err, validation_seconds, validation_timeout = run(cmd, None, timeout + dolmen_timeout + 10.0)
    (per_file_out / "validator.stdout").write_text(out, encoding="utf-8")
    (per_file_out / "validator.stderr").write_text(err, encoding="utf-8")

    outcome = "missing-summary"
    message = ""
    summary = per_file_out / "summary.csv"
    if summary.exists():
        with summary.open(newline="", encoding="utf-8") as handle:
            summary_rows = list(csv.DictReader(handle))
        if summary_rows:
            outcome = summary_rows[-1].get("outcome", outcome)
            message = summary_rows[-1].get("message", "")
    elif validation_timeout:
        outcome = "validator-timeout"
        message = "validator wrapper timeout"

    row["validation_outcome"] = outcome
    row["validation_seconds"] = f"{validation_seconds:.6f}"
    row["message"] = message
    rows.append(row)

    if outcome in {"model-error", "solver-error", "read-error", "validation-unavailable"}:
        stop_reason = f"stopped on {outcome}: {path}"
        break

for index, path in enumerate(sat_files[:perf_limit]):
    text = path.read_text(encoding="utf-8", errors="replace")
    plain_rc, plain_out, plain_err, plain_seconds, plain_timeout = run(
        [solver, "--quiet", str(path)], None, perf_timeout
    )
    model_input = shard / f"perf-{index:03d}.model.smt2"
    model_input.write_text(model_problem_text(text), encoding="utf-8")
    model_rc, model_out, model_err, model_seconds, model_timeout = run(
        [solver, "--quiet", str(model_input)], None, perf_timeout
    )
    perf_rows.append(
        {
            "file": str(path),
            "plain_answer": first_answer(plain_out) if not plain_timeout else "timeout",
            "plain_exit": "" if plain_rc is None else str(plain_rc),
            "plain_seconds": f"{plain_seconds:.6f}",
            "model_answer": first_answer(model_out) if not model_timeout else "timeout",
            "model_exit": "" if model_rc is None else str(model_rc),
            "model_seconds": f"{model_seconds:.6f}",
            "model_over_plain": ""
            if plain_seconds <= 0
            else f"{(model_seconds / plain_seconds):.6f}",
        }
    )

with (shard / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
    writer = csv.DictWriter(
        handle,
        fieldnames=[
            "file",
            "z3_answer",
            "z3_exit",
            "z3_seconds",
            "validation_outcome",
            "validation_seconds",
            "message",
        ],
    )
    writer.writeheader()
    writer.writerows(rows)

with (shard / "perf.csv").open("w", newline="", encoding="utf-8") as handle:
    writer = csv.DictWriter(
        handle,
        fieldnames=[
            "file",
            "plain_answer",
            "plain_exit",
            "plain_seconds",
            "model_answer",
            "model_exit",
            "model_seconds",
            "model_over_plain",
        ],
    )
    writer.writeheader()
    writer.writerows(perf_rows)

sat_count = sum(1 for row in rows if row["z3_answer"] == "sat")
pass_count = sum(1 for row in rows if row["validation_outcome"] == "pass")
timeout_count = sum(1 for row in rows if row["validation_outcome"] in {"solver-timeout", "dolmen-timeout", "validator-timeout"})
print(f"shard={task_id} files={len(files)} z3_sat={sat_count} model_pass={pass_count} model_timeouts={timeout_count}")
if stop_reason:
    print(stop_reason, file=sys.stderr)
    raise SystemExit(1)
PY
