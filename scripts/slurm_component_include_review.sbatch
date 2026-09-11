#!/usr/bin/env bash
# Advisory whole-tree include scan, with durable per-file output and four workers.
#SBATCH --job-name=primo-component-includes
#SBATCH --cpus-per-task=4
#SBATCH --mem=12G
#SBATCH --time=00:20:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/clang-tools-env/bin:$PATH"
./build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
python3 - <<'PY'
import concurrent.futures
import hashlib
import json
import os
import subprocess
import time
from pathlib import Path

out = Path(os.environ["PRIMO_REVIEW_OUT_DIR"])
out.mkdir(parents=True, exist_ok=True)
sources = sorted(path for folder in (Path("src"), Path("tests"))
                 for path in folder.rglob("*.cpp"))
headers = Path("contrib/local/clang-tools-env/cxx-headers/include-paths.txt").read_text().splitlines()
assert sources and all(Path(path).is_dir() for path in headers)
arguments = [argument for path in headers for argument in ("--extra-arg=-isystem", "--extra-arg=" + path)]
def scan(item):
    index, source = item
    command = ["clang-tidy", "-p", "build-slurm-rel", "-checks=-*,clang-diagnostic-*,misc-include-cleaner",
               *arguments, str(source)]
    start = time.monotonic()
    with (out / f"file-{index:03d}.log").open("w") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=600)
    return {"file": str(source), "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "exit": result.returncode, "seconds": time.monotonic()-start, "log": f"file-{index:03d}.log"}
rows = []
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    futures = [pool.submit(scan, item) for item in enumerate(sources)]
    for future in concurrent.futures.as_completed(futures):
        row = future.result()
        rows.append(row)
        with (out / "progress.jsonl").open("a") as stream:
            stream.write(json.dumps(row) + "\n")
        print(row["file"], row["exit"], flush=True)
(out / "summary.json").write_text(json.dumps(sorted(rows, key=lambda row:row["file"]), indent=2))
assert len(rows) == len(sources) and all(row["exit"] == 0 for row in rows)
PY
