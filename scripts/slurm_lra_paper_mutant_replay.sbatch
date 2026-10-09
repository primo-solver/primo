#!/usr/bin/env bash
# Reproduce the dated paper-profile campaign's status-aware postcheck.
# These three retained-input roots and the binary revision are intentional.
#SBATCH --job-name=primo-lra-mutant-replay
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=01:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
task=${SLURM_ARRAY_TASK_ID:-0}
shards=16
root=$((task / shards))
shard=$((task % shards))
roots=(model-fuzz/seeded-mutation/task-1 model-fuzz/seeded-mutation/task-2 model-fuzz/seeded-opfuzz/task-0)
base=results/lra-reverification-20261009
out="$base/mutant-replay/root-$root/shard-$shard"
mkdir -p "$out"
./build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx 'git: 23f945d' "$out/version.txt"
python3 - "$base/${roots[$root]}/scratch" "$out" "$shard" "$shards" <<'PY'
import hashlib,json,pathlib,sys
source,out=map(pathlib.Path,sys.argv[1:3])
shard,shards=map(int,sys.argv[3:])
files=sorted(source.rglob('*.smt2'))
if not files:
    raise SystemExit('no retained mutants')
selected=files[shard::shards]
inputs=out/'inputs'
inputs.mkdir()
identities={}
for i,path in enumerate(selected):
    (inputs/(str(i)+'.smt2')).symlink_to(path.resolve())
    identities[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
(out/'provenance.json').write_text(json.dumps({'source':str(source),'total_mutants':len(files),'shard':shard,'shards':shards,'inputs':identities},indent=2)+'\n')
PY
python3 scripts/check_solver_seed_corpus.py --seeds "$out/inputs" \
  --ignore-declared-status --solver ./build-slurm-rel/primo \
  --solver-arg=--quiet --solver-arg=--lra-model-phase --solver-arg=--monotone-elimination \
  --ref scripts/yices_smt2_ref.sh --timeout 5 --out "$out/answers.csv"
