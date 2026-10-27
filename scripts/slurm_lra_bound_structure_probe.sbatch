#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-bound-structure
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_DIR:?}:$PWD/contrib/local/bin:$PATH"
OUT=${PRIMO_PROBE_OUT:?}
mkdir -p "$OUT"
build-slurm-rel/primo --version > "$OUT/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$OUT/version.txt"
case "${PRIMO_PROBE_PHASE:?}" in
  check)
    ctest --test-dir build-slurm-rel --timeout 120 --output-on-failure
    python3 -m unittest discover -s tests/python -p 'test_*.py'
    python3 scripts/check_doc_examples.py --solver build-slurm-rel/primo
    ;;
  asan)
    probe_cxxflags=""
    while IFS= read -r header; do
      probe_cxxflags="$probe_cxxflags -isystem $header"
    done < tmp/cxx-header-paths.txt
    export CXXFLAGS="$probe_cxxflags"
    export CMAKE_BUILD_PARALLEL_LEVEL=${SLURM_CPUS_PER_TASK:-2}
    asan_config=(-DCMAKE_BUILD_TYPE=Debug -DPRIMO_SANITIZERS=address
      "-DCMAKE_PREFIX_PATH=$PWD/contrib/local"
      "-DPRIMO_GIT_REVISION_OVERRIDE=$PRIMO_EXPECTED_GIT")
    if [[ -n ${PRIMO_ASAN_RUNTIME_DIR:-} ]]; then
      test -f "$PRIMO_ASAN_RUNTIME_DIR/libasan.so"
      asan_config+=("-DCMAKE_EXE_LINKER_FLAGS=-L$PRIMO_ASAN_RUNTIME_DIR")
      export LD_LIBRARY_PATH="$PRIMO_ASAN_RUNTIME_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    fi
    cmake -S . -B build-bound-probe-asan "${asan_config[@]}"
    # Reuse the prepared, hash-recorded dependencies; rebuild owned code with ASan.
    make -o contrib-required asan-test ASAN_BUILD_DIR=build-bound-probe-asan
    ;;
  fuzz)
    python3 scripts/qf_lra_boolean_arithmetic_fuzz.py \
      --solver build-slurm-rel/primo --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
      --solver-arg=--lra-row-propagation --solver-arg=--lra-bidirectional-row-propagation \
      --solver-arg=--lra-bound-probe --solver-arg="$OUT/current-trace.jsonl" \
      --solver-arg=--lra-bound-probe-limit --solver-arg=200 \
      --solver-arg=--lra-bound-probe-snapshot-limit --solver-arg=20 \
      --count 1000 --seed 2026102601 --timeout 5 --dolmen-timeout 10 --out "$OUT/fuzz"
    ;;
  long-probe)
    python3 - <<'PYCODE'
import json
import os
import subprocess
import sys
from pathlib import Path

plan = json.loads(Path(os.environ['PRIMO_PROBE_PLAN']).read_text())
index = int(os.environ['SLURM_ARRAY_TASK_ID'])
task = plan['inputs'][index]
parent = Path(os.environ['PRIMO_PROBE_OUT']) / ('case-%03d' % index)
parent.mkdir(parents=True, exist_ok=False)
manifest = parent / 'manifest.txt'
manifest.write_text(task['file'] + '\n')
command = [sys.executable, 'scripts/run_lra_bound_probe.py',
    '--solver', 'build-slurm-rel/primo', '--reference', 'contrib/local/bin/z3',
    '--dolmen', 'contrib/local/bin/dolmen', '--manifest', str(manifest),
    '--root', 'tmp/QF_LRA', '--out', str(parent / 'run'),
    '--timeout', str(task['timeout_seconds']), '--limit', str(plan['causal_limit']),
    '--snapshot-limit', str(plan['snapshot_limit']),
    '--model-timeout', str(task['model_timeout_seconds']), '--dolmen-timeout', '30']
execution = {'command': command, 'task': task, 'job_id': os.environ['SLURM_JOB_ID'],
             'array_job_id': os.environ['SLURM_ARRAY_JOB_ID'], 'index': index,
             'cpus': os.environ['SLURM_CPUS_PER_TASK'],
             'affinity': sorted(os.sched_getaffinity(0))}
(parent / 'execution.json').write_text(json.dumps(execution, indent=2) + '\n')
subprocess.run(command, check=True)
PYCODE
    ;;
  probe)
    python3 scripts/run_lra_bound_probe.py \
      --solver build-slurm-rel/primo --reference contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
      --manifest benchmarks/lra-bound-probe-examples.txt --root benchmarks/smoke/lra-bound-probe \
      --constructed --timeout 5 --limit 20000 --out "$OUT/examples"
    python3 scripts/run_lra_bound_probe.py \
      --solver build-slurm-rel/primo --reference contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
      --manifest benchmarks/qf-lra-explanation-probe.txt --root sandbox/non-incremental/QF_LRA \
      --timeout 3 --limit 4000 --out "$OUT/corpus"
    ;;
  *) echo 'invalid PRIMO_PROBE_PHASE' >&2; exit 2 ;;
esac
