#!/usr/bin/env bash
#SBATCH --job-name=primo-soi-profiles-fuzz
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=01:00:00
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
outer=${SLURM_ARRAY_TASK_ID:?}
profile=$((outer / 4))
shard=$((outer % 4))
export PRIMO_FUZZ_SOLVER
PRIMO_FUZZ_SOLVER=$(python3 - "$PRIMO_SOI_PLAN" "$profile" "$PWD/build-slurm-rel/primo" <<'PY'
import json, shlex, sys
plan = json.load(open(sys.argv[1]))
configs = [c for c in plan['configurations'] if c['binary'] == 'candidate']
config = configs[int(sys.argv[2])]
print(shlex.join([sys.argv[3], *plan['common_args'], *config['args'], '--check-model']))
PY
)
name=$(python3 - "$PRIMO_SOI_PLAN" "$profile" <<'PY'
import json, sys
configs = [c for c in json.load(open(sys.argv[1]))['configurations'] if c['binary'] == 'candidate']
print(configs[int(sys.argv[2])]['id'])
PY
)
export PRIMO_FUZZ_OUT_ROOT="$PWD/fuzz-results/final-profiles/$name"
mkdir -p "$PRIMO_FUZZ_OUT_ROOT"
printf '%s %s %s\n' "$SLURM_JOB_ID" "$outer" "$PRIMO_FUZZ_SOLVER" > "$PRIMO_FUZZ_OUT_ROOT/dispatch-$shard.txt"
export PRIMO_FUZZ_FUZZSMT_SHARDS=4 PRIMO_FUZZ_FUZZSMT_COUNT=250
export PRIMO_FUZZ_FUZZSMT_LOGIC=QF_LRA PRIMO_FUZZ_FUZZSMT_REF=z3
export PRIMO_FUZZ_FUZZSMT_TIMEOUT=5 PRIMO_FUZZ_FUZZSMT_SEED_BASE=611520
export PRIMO_FUZZ_OPFUZZ_TASKS=0 PRIMO_FUZZ_YINYANG_SHARDS=0
export PRIMO_FUZZ_OPFUZZ_SEEDS="$PWD/benchmarks/seeds/qf_lra"
export PRIMO_FUZZ_YINYANG_SEEDS="$PWD/benchmarks/seeds/qf_lra"
export SLURM_ARRAY_TASK_ID="$shard"
exec bash scripts/slurm_qfuf_fuzz_gate.sbatch
