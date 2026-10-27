#!/usr/bin/env bash
# Qualification of a frozen main/migration merge; no performance measurements.
#SBATCH --job-name=primo-migration-main-check
#SBATCH --partition=compute
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=12G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PWD/contrib/local/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=${SLURM_CPUS_PER_TASK:-4}
export CTEST_PARALLEL_LEVEL=${SLURM_CPUS_PER_TASK:-4}
export CTEST_TEST_TIMEOUT=120
out="${PRIMO_MERGE_OUT:?}/${PRIMO_MERGE_PHASE:?}${PRIMO_MERGE_ATTEMPT:+-$PRIMO_MERGE_ATTEMPT}"
export PRIMO_MERGE_PHASE_OUT=$out
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/stdout.log") 2> >(tee "$out/stderr.log" >&2)
trap 'status=$?; printf "%s\n" "$status" > "$out/exit-code"' EXIT
build-slurm-rel/primo --version | tee "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_REVISION:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/executables.sha256"
case "$PRIMO_MERGE_PHASE" in
  checks)
    # Refresh the captured CLI fixture from this binary; collect and review its
    # diff before accepting the merge. Native sources remain the frozen tree.
    python3 - <<'PY'
import subprocess
from pathlib import Path
solver = str(Path('build-slurm-rel/primo').resolve())
text = subprocess.check_output([solver, '--help'], text=True)
text = text.replace(f'{solver} [OPTIONS]', 'primo [OPTIONS]', 1)
Path('docs/examples/primo-help.txt').write_text(
    '\n'.join(line.rstrip() for line in text.splitlines()).rstrip() + '\n')
PY
    cpp_files=$(python3 -c 'from pathlib import Path; print(" ".join(str(p) for root in ("src", "tests") for p in Path(root).rglob("*") if p.suffix in (".cpp", ".hpp", ".h")))')
    make -o contrib-required check BUILD_DIR=build-slurm-rel CPP_FORMAT_FILES="$cpp_files"
    python3 scripts/check_doc_examples.py --solver build-slurm-rel/primo
    ;;
  asan)
    while IFS= read -r header; do
      [[ $header =~ ^[a-zA-Z0-9_./-]+$ ]] && test -d "$header"
      export CXXFLAGS="${CXXFLAGS:-} -isystem $header"
    done < "${PRIMO_CXX_HEADER_MANIFEST:?}"
    runtime=${PRIMO_ASAN_RUNTIME_DIR:?}
    test -f "$runtime/libasan.so"
    export LD_LIBRARY_PATH="$runtime${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    cmake -S . -B build-merge-asan -DCMAKE_BUILD_TYPE=Debug -DPRIMO_SANITIZERS=address \
      "-DCMAKE_PREFIX_PATH=$PWD/contrib/local" \
      "-DCMAKE_EXE_LINKER_FLAGS=-L$runtime" \
      "-DPRIMO_GIT_REVISION_OVERRIDE=$PRIMO_EXPECTED_REVISION"
    make -o contrib-required asan-test ASAN_BUILD_DIR=build-merge-asan
    ;;
  lra)
    python3 scripts/qf_lra_boolean_arithmetic_fuzz.py \
      --solver build-slurm-rel/primo --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
      --solver-arg=--quiet --solver-arg=--lra-model-phase --solver-arg=--monotone-elimination \
      --count 2000 --seed 2026102701 --timeout 5 --dolmen-timeout 10 --out "$out/default"
    python3 scripts/qf_lra_boolean_arithmetic_fuzz.py \
      --solver build-slurm-rel/primo --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
      --solver-arg=--quiet --solver-arg=--lra-equality-propagation \
      --solver-arg=--lra-row-propagation --solver-arg=--lra-bidirectional-row-propagation \
      --count 1000 --seed 2026102702 --timeout 5 --dolmen-timeout 10 --out "$out/equality"
    ;;
  static)
    python3 - <<'PY' > "$out/translation-units.txt"
from pathlib import Path
for root in ('src', 'tests'):
    for path in sorted(Path(root).rglob('*.cpp')):
        print(path)
PY
    xargs -a "$out/translation-units.txt" -n1 -P "${SLURM_CPUS_PER_TASK:-4}" \
      clang-tidy -p build-slurm-rel -checks='clang-analyzer-*,misc-include-cleaner' \
      > "$out/clang-tidy.log" 2>&1
    headers=()
    while IFS= read -r header; do
      [[ $header =~ ^[a-zA-Z0-9_./-]+$ ]] && test -d "$header"
      headers+=(-isystem "$header")
    done < "${PRIMO_CXX_HEADER_MANIFEST:?}"
    for source in docs/examples/*.cpp; do
      clang-tidy "$source" -checks='clang-analyzer-*,misc-include-cleaner' \
        -- -std=c++20 -Isrc -Icontrib/local/include "${headers[@]}" \
        >> "$out/examples-tidy.log" 2>&1
    done
    ;;
  report)
    python3 scripts/qualification/summarize_public_launch.py \
      --root results/public-launch --initial "${PRIMO_GATE_JOB:?}" \
      --integer "${PRIMO_INTEGER_JOB:?}" --sessions "$PRIMO_GATE_JOB" \
      --candidate "$PRIMO_EXPECTED_REVISION" --out "$out/generated.json"
    python3 - <<'PY'
import csv
import hashlib
import json
import os
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, 'scripts/qualification/publication_followups')
from summarize import budget_summary, mutation_summary

root = Path(os.environ['PRIMO_MERGE_OUT'])
report_directory = Path(os.environ['PRIMO_MERGE_PHASE_OUT'])
report = json.loads((report_directory / 'generated.json').read_text())
for shard in report['shards']:
    shard['budget_replay'] = budget_summary(Path(shard['artifact']) / 'budget-replay')
mutations = Path('results/public-mutations') / os.environ['PRIMO_MUTATION_JOB']
report['mutations'] = [mutation_summary(mutations / f'shard-{slot}',
    os.environ['PRIMO_EXPECTED_REVISION'], report['shards'][0]['binaries'])
    for slot in range(2)]
report['additional_lra'] = {}
lra_directory = Path(os.environ.get('PRIMO_LRA_OUT', str(root / 'lra')))
for profile, expected in (('default', 2000), ('equality', 1000)):
    summary = lra_directory / profile / 'summary.csv'
    with summary.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == expected
    assert all(row['outcome'] == 'PASS' and row['dolmen'] == 'PASS' for row in rows)
    report['additional_lra'][profile] = {
        'cases': len(rows), 'outcomes': dict(Counter(row['outcome'] for row in rows)),
        'summary_sha256': hashlib.sha256(summary.read_bytes()).hexdigest()}
(report_directory / 'qualification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'routes': report['routes'], 'mutations': report['mutations'],
                  'additional_lra': report['additional_lra']}, indent=2))
PY
    ;;
  tooling)
    cd "${PRIMO_TOOLING_WORKDIR:?}"
    # Git archive uses commit timestamps; align Make's input with this node's
    # clock without changing its contents or the frozen native workspace.
    touch Makefile
    cpp_files=$(python3 -c 'from pathlib import Path; print(" ".join(str(p) for root in ("src", "tests") for p in Path(root).rglob("*") if p.suffix in (".cpp", ".hpp", ".h")))')
    make lint CPP_FORMAT_FILES="$cpp_files"
    python3 -m unittest discover -s tests/python -p 'test_*.py'
    ;;
  *) echo 'invalid PRIMO_MERGE_PHASE' >&2; exit 2 ;;
esac
