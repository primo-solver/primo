"""Local option sweep over the 10 tropical-matrix QF_LRA files.

Diagnostic harness for docs/qf-lra-heuristics.md's tropical-matrix work: run
one Primo configuration per pass over benchmarks/qf-lra-tropical-matrix.txt and
record solved/unsolved plus the arithmetic counters that say *why*.

Two implementation details matter and were both learned the hard way:

  * Timeouts use the shell `timeout` command, which sends SIGTERM. Primo
    flushes partial `--stats` counters when SIGTERM is observed cooperatively,
    so timeout rows still carry pivot and propagation counts.
    `subprocess.run(timeout=...)` SIGKILLs and loses them, which is exactly the
    data a negative result needs. A row interrupted inside a long parser,
    frontend, or preprocessing phase is answered by the `--cancel-grace-seconds`
    watchdog instead and carries `unknown` with no counters.
  * The propagation counters are `:lra.row-propagations` and
    `:lra.bidirectional-row-propagations` -- not `...-propagation.derived`.

Usage:
  python3 scripts/bench_tropical_matrix_sweep.py            # every config
  python3 scripts/bench_tropical_matrix_sweep.py t1_only    # one config
"""
import csv
import os
import re
import subprocess
import sys
import time

BASE = "sandbox/non-incremental/QF_LRA/tropical-matrix"
MANIFEST = "benchmarks/qf-lra-tropical-matrix.txt"
OUT_DIR = "results/tropical-t1-propagation"
WORKDIR = "/workspace"
TIMEOUT = 60

CAP_FLAGS = [
    "--lra-row-propagation-max-fanout", "--lra-row-propagation-max-row-size",
    "--lra-bidirectional-row-propagation-max-fanout",
    "--lra-bidirectional-row-propagation-max-row-size",
]
ROW_PROP = ["--lra-row-propagation", "--lra-bidirectional-row-propagation"]
T1 = ["--guarded-real-equality-lowering"]

CONFIGS = {
    "t1_only": T1,
    "t1_rowprop_default_caps": T1 + ROW_PROP,
}
for _cap in (128, 1024):
    _args = T1 + ROW_PROP
    for _flag in CAP_FLAGS:
        _args = _args + [_flag, str(_cap)]
    CONFIGS[f"t1_rowprop_{_cap}"] = _args

FIELDS = ["file", "status", "answer", "seconds", "lra_pivots", "lra_term_ops",
          "row_props", "bidi_props", "guarded_distributed"]


def manifest_files():
    files = []
    with open(os.path.join(WORKDIR, MANIFEST)) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if line:
                files.append(line)
    return files


def stat(text, name):
    m = re.search(r':' + re.escape(name) + r'\s+(\S+)', text)
    return m.group(1) if m else ""


def run(name, smt_file, args):
    errpath = os.path.join(WORKDIR, OUT_DIR, f"{name}-{smt_file}.err")
    cmd = (["timeout", "-k", "5", str(TIMEOUT), "build/primo", "--quiet",
            "--stats"] + args + [os.path.join(BASE, smt_file)])
    started = time.time()
    with open(errpath, "w") as errfh:
        proc = subprocess.run(cmd, capture_output=False, stdout=subprocess.PIPE,
                              stderr=errfh, text=True, cwd=WORKDIR, check=False)
    elapsed = time.time() - started
    with open(errpath) as errfh:
        err = errfh.read()
    answer = "unknown"
    if re.search(r'^sat$', proc.stdout or "", re.MULTILINE):
        answer = "sat"
    elif re.search(r'^unsat$', proc.stdout or "", re.MULTILINE):
        answer = "unsat"
    return {
        "file": smt_file,
        "status": "ok" if answer in ("sat", "unsat") else "timeout",
        "answer": answer,
        "seconds": round(elapsed, 3),
        "lra_pivots": stat(err, "lra.pivots"),
        "lra_term_ops": stat(err, "lra.pivot-term-ops"),
        "row_props": stat(err, "lra.row-propagations"),
        "bidi_props": stat(err, "lra.bidirectional-row-propagations"),
        "guarded_distributed": stat(
            err, "preprocessing.real-equality-lowering.guarded-distributed"),
    }


def sweep(name, args, files):
    os.makedirs(os.path.join(WORKDIR, OUT_DIR), exist_ok=True)
    outpath = os.path.join(WORKDIR, OUT_DIR, f"{name}.csv")
    with open(outpath, "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS)
        writer.writeheader()
        for smt_file in files:
            row = run(name, smt_file, args)
            writer.writerow(row)
            fh.flush()
            print(name, row["file"], row["status"], row["answer"],
                  row["seconds"], f"pivots={row['lra_pivots']}",
                  f"row-props={row['row_props']}", flush=True)


if __name__ == "__main__":
    only = sys.argv[1] if len(sys.argv) > 1 else None
    all_files = manifest_files()
    for config_name, config_args in CONFIGS.items():
        if only and config_name != only:
            continue
        sweep(config_name, config_args, all_files)
