#!/usr/bin/env bash
# Before submission: prepare and --check-only with
# scripts/slurm_prepare_benchmark_workspace.py for the intended commit.
# Four bounded shards cover 1000 sessions, all five admitted logics.
#SBATCH --job-name=primo-incremental
#SBATCH --array=0-3
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err

set -euo pipefail
WORKDIR=${PRIMO_WORKDIR:?prepared workspace required}
OUT_ROOT=${PRIMO_INCREMENTAL_OUT:?fresh artifact directory required}
EXPECTED_REVISION=${PRIMO_EXPECTED_REVISION:?expected git revision required}
SOLVER=${PRIMO_INCREMENTAL_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
cd "$WORKDIR"
export PATH="$WORKDIR/contrib/local/bin:$PATH"
version=$("$SOLVER" --version)
if [[ "$version" != *"$EXPECTED_REVISION"* ]]; then
  echo "solver revision does not match expected revision" >&2
  exit 2
fi
extra_args=(--solver-arg=--quiet)
if [[ -n ${PRIMO_INCREMENTAL_SOLVER_ARGS:-} ]]; then
  # Whitespace-separated tokens only, matching the existing mutation gate.
  read -r -a words <<< "$PRIMO_INCREMENTAL_SOLVER_ARGS"
  for arg in "${words[@]}"; do extra_args+=(--solver-arg="$arg"); done
fi
python3 scripts/validate_incremental.py \
  --solver "$SOLVER" "${extra_args[@]}" \
  --ref "$WORKDIR/contrib/local/bin/z3" \
  --count 250 --seed "$((202609100 + SLURM_ARRAY_TASK_ID * 250))" \
  --timeout 10 --dolmen-timeout 20 \
  --out "$OUT_ROOT/shard-$SLURM_ARRAY_TASK_ID"
