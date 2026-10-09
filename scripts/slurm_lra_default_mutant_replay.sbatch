#!/usr/bin/env bash
# Independent exit-status-aware replay of every retained default-profile mutant.
# Non-pass rows fail their shard and require classification before promotion.
#SBATCH --job-name=primo-lra-default-mutants
#SBATCH --partition=compute
#SBATCH --array=0-15%8
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=01:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
shard=${SLURM_ARRAY_TASK_ID:?}
out="${PRIMO_CAMPAIGN_OUT:?}/shard-$shard"
mkdir -p "$out" logs
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo > "$out/binary.sha256"
python3 - "${PRIMO_MUTANT_ROOT:?}" "$out" "$shard" <<'SHARD'
import hashlib,json,pathlib,sys
source,out=map(pathlib.Path,sys.argv[1:3])
shard=int(sys.argv[3])
files=sorted(source.rglob('*.smt2'))
if len(files)<16:
    raise SystemExit(f'insufficient retained mutants: {len(files)}')
inputs=out/'inputs'
inputs.mkdir()
identities={}
for index,path in enumerate(files[shard::16]):
    (inputs/(str(index)+'.smt2')).symlink_to(path.resolve())
    identities[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
data={'source':str(source),'total_mutants':len(files),'shard':shard,
      'shards':16,'solver_args':['--quiet'],'reference':'scripts/yices_smt2_ref.sh',
      'timeout_seconds':5,'inputs':identities}
(out/'provenance.json').write_text(json.dumps(data,indent=2)+'\n')
SHARD
python3 scripts/check_solver_seed_corpus.py --seeds "$out/inputs" \
  --ignore-declared-status --solver build-slurm-rel/primo --solver-arg=--quiet \
  --ref scripts/yices_smt2_ref.sh --timeout 5 --out "$out/answers.csv"
