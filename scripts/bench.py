#!/usr/bin/env python3
"""Run reproducible benchmarks and emit CSV."""

from __future__ import annotations

import argparse
import csv
import subprocess
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out", type=Path, default=Path("results/bench.csv"))
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    output_dir = args.out.parent / "bench-output"
    output_dir.mkdir(parents=True, exist_ok=True)

    rows = []
    for path in args.files:
        stdout_path = output_dir / f"{path.name}.stdout"
        stderr_path = output_dir / f"{path.name}.stderr"
        start = time.perf_counter()
        timeout = False
        try:
            proc = subprocess.run(
                [args.solver, str(path)],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=args.timeout,
            )
            exit_code = proc.returncode
            stdout = proc.stdout
            stderr = proc.stderr
        except subprocess.TimeoutExpired as exc:
            timeout = True
            exit_code = 124
            stdout = exc.stdout or ""
            stderr = exc.stderr or ""
        runtime = time.perf_counter() - start
        stdout_path.write_text(stdout)
        stderr_path.write_text(stderr)
        first = stdout.strip().splitlines()[0] if stdout.strip() else ""
        result = first if first in {"sat", "unsat", "unknown", "unsupported"} else "error"
        rows.append({
            "file": str(path),
            "result": result,
            "runtime": f"{runtime:.6f}",
            "timeout": str(timeout).lower(),
            "exit_code": exit_code,
            "stdout path": str(stdout_path),
            "stderr path": str(stderr_path),
        })
        print(f"{path}: {result} {runtime:.3f}s")

    with args.out.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["file", "result", "runtime", "timeout", "exit_code", "stdout path", "stderr path"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
