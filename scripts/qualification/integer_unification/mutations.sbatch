#!/usr/bin/env bash
# Ground, logic-preserving real-seed mutations; retain inputs for strict replay.
#SBATCH --job-name=primo-integer-mutations
#SBATCH --array=0-1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=01:30:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo 'SLURM allocation required' >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_YINYANG_BIN:?}:$PWD/contrib/local/bin:$PATH"
command -v opfuzz
slot=${SLURM_ARRAY_TASK_ID:?}
logics=(QF_LIA QF_UFLIA)
(( slot < ${#logics[@]} )) || exit 2
logic=${logics[$slot]}
iterations=20
out="results/integer-mutations/${SLURM_ARRAY_JOB_ID}/shard-$slot"
corpus="$out/seeds"
test ! -e "$out"
mkdir -p "$out"
exec > >(tee "$out/stdout.log") 2> >(tee "$out/stderr.log" >&2)
trap 'status=$?; printf "%s\n" "$status" > "$out/exit-code"' EXIT
set -x
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_REVISION:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/dolmen > "$out/executables.sha256"
python3 -m pip show yinyang z3-solver > "$out/mutator-version.txt"
# Select five small, existing preservation inputs and verify their frozen
# corpus hashes before copying. Mutations never edit the original corpus.
export PRIMO_MUTATION_LOGIC="$logic" PRIMO_MUTATION_OUT="$out"
python3 - <<'PYCODE'
import hashlib
import json
import os
import shutil
from pathlib import Path
logic = os.environ['PRIMO_MUTATION_LOGIC']
out = Path(os.environ['PRIMO_MUTATION_OUT'])
root = Path(os.environ['PRIMO_CORPUS_ROOT']) / logic
frozen = json.loads((Path(os.environ['PRIMO_BASELINE_MANIFEST_DIR']) / (logic + '.json')).read_text())
by_path = {row['path']: row for row in frozen}
manifest = Path('benchmarks') / ('incremental-preservation-' + logic.lower() + '.txt')
selected = sorted((by_path[p] for p in manifest.read_text().splitlines()),
                  key=lambda row: (row['bytes'], row['path']))[:5]
assert len(selected) == 5
(out / 'seeds').mkdir()
for index, row in enumerate(selected):
    source = root / row['path']
    assert hashlib.sha256(source.read_bytes()).hexdigest() == row['sha256'], source
    target = out / 'seeds' / ('seed-%03d.smt2' % index)
    shutil.copyfile(source, target)
    row['copied_as'] = str(target)
(out / 'seed-provenance.json').write_text(json.dumps(selected, indent=2) + '\n')
PYCODE
sha256sum "$corpus"/*.smt2 > "$out/seeds.sha256"
python3 scripts/check_solver_seed_corpus.py --seeds "$corpus" \
  --solver build-slurm-rel/primo --solver-arg=--quiet --ref contrib/local/bin/z3 \
  --timeout 30 --out "$out/seed-answers.csv"
scripts/yinyang_fuzz.sh --seeds "$corpus" --typefuzz "$PWD/scripts/seeded_opfuzz.py" \
  --config scripts/fuzz_configs/model_linear_ops.txt \
  --solver "$PWD/build-slurm-rel/primo --quiet" --ref 'z3 model_validate=true' \
  --seconds 900 --iterations "$iterations" --solver-timeout 5 --file-size-limit 2000000 \
  --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs" \
  --random-seed "$((${PRIMO_SEED_BASE:-202610020} + slot))" --keep-logic --hoist-declarations --keep-mutants
answer_status=0
python3 scripts/check_solver_seed_corpus.py --seeds "$out/scratch" \
  --ignore-declared-status --solver build-slurm-rel/primo --solver-arg=--quiet \
  --ref contrib/local/bin/z3 --timeout 10 --out "$out/mutant-answers.csv" || answer_status=$?
# Model replay uses only successful SAT inputs. Resource limits stay incomplete.
if (( answer_status != 0 )); then
  python3 scripts/classify_mutation_resources.py answers "$out/mutant-answers.csv"
fi
for stage in seed mutant; do
  python3 scripts/select_fuzz_sat_inputs.py "$out/$stage-answers.csv" --out "$out/$stage-sat.txt"
  if [[ -s "$out/$stage-sat.txt" ]]; then
    python3 scripts/validate_model.py --solver build-slurm-rel/primo --solver-arg=--quiet \
      --dolmen contrib/local/bin/dolmen --require-dolmen-pass --expect-sat \
      --dolmen-arg=--strict=false --timeout 30 --dolmen-timeout 30 \
      --out "$out/$stage-models" --manifest "$out/$stage-sat.txt"
  fi
done
python3 - "$out/scratch" <<'PYCODE'
import sys
from pathlib import Path
count = len(list(Path(sys.argv[1]).rglob('*.smt2')))
assert count >= 100, ('insufficient retained mutation coverage', count)
print('retained mutation inputs:', count)
PYCODE
if [[ -d $out/bugs ]] && [[ -n $(find "$out/bugs" -type f -print -quit) ]]; then
  echo 'saved mutation failures require classification' >&2
  exit 1
fi
