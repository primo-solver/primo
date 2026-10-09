#!/usr/bin/env bash
# Dated paper-profile seed mutation. Require the independent retained-input
# replay and diagnostic classification before accepting correctness evidence.
#SBATCH --job-name=primo-lra-seeded-mutation
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PWD/.venv-yinyang/bin:$PATH"
task=${SLURM_ARRAY_TASK_ID:-0}
seed=$((2026101000 + task))
if (( task == 0 )); then
  out=results/lra-reverification-20261009/model-fuzz/seeded-opfuzz/task-$task
else
  out=results/lra-reverification-20261009/model-fuzz/seeded-mutation/task-$task
fi
seeds=benchmarks/seeds/qf_lra_division
mkdir -p "$out"
# The tracked regression seeds already passed all four profiles in precheck.
# Recheck them here before mutating the broader, generated family seed corpus.
for regression in qf_lra qf_lra_real; do
  python3 scripts/check_solver_seed_corpus.py --seeds "benchmarks/seeds/$regression" \
    --solver build-slurm-rel/primo --solver-arg=--quiet \
    --solver-arg=--lra-model-phase --solver-arg=--monotone-elimination \
    --ref contrib/local/bin/z3 --timeout 30 --out "$out/regression-$regression.csv"
done
args=(--seeds "$seeds" --solver 'build-slurm-rel/primo --quiet --lra-model-phase --monotone-elimination'
      --ref 'scripts/yices_smt2_ref.sh' --seconds 1500
      --solver-timeout 5 --file-size-limit 200000 --keep-logic --keep-mutants
      --random-seed "$seed" --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs")
if (( task == 0 )); then
  args+=(--typefuzz "$PWD/scripts/seeded_opfuzz.py" --iterations 200 --config benchmarks/fuzz-configs/qf_lra_operator_mutations.txt)
else
  args+=(--iterations 100 --hoist-declarations --config benchmarks/fuzz-configs/qf_lra_typefuzz.txt)
fi
scripts/yinyang_fuzz.sh "${args[@]}"
python3 - "$out" <<'CHECK'
import pathlib,sys
bugs=pathlib.Path(sys.argv[1])/'bugs'
if bugs.exists() and any(x.is_file() for x in bugs.rglob('*')):
    raise SystemExit('mutation failures require triage')
CHECK
