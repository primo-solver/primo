#!/usr/bin/env bash
# Sharded Dolmen validation over a real QF_UFLRA benchmark manifest.
#
# The default manifest is the deterministic five-per-family sample. Relative
# manifest paths are resolved below PRIMO_MODEL_ROOT. The validator writes and
# fsyncs every completed row, so a timeout leaves an auditable prefix.
#SBATCH --job-name=primo-qfuflra-models
#SBATCH --array=0-3
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the prepared remote workspace}
ROOT=${PRIMO_MODEL_ROOT:-"$WORKDIR/tmp/QF_UFLRA"}
MANIFEST=${PRIMO_MODEL_MANIFEST:-"$WORKDIR/benchmarks/qf_uflra-smtlib-sample-5-per-family.txt"}
OUT_ROOT=${PRIMO_MODEL_OUT_ROOT:-"$WORKDIR/slurm-results/qf-uflra-model-validation-${SLURM_ARRAY_JOB_ID:-manual}"}
SHARDS=${PRIMO_MODEL_SHARDS:-4}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}
SOLVER=${PRIMO_MODEL_SOLVER:-"$WORKDIR/build-slurm-rel/primo"}
SOLVER_ARGS_TEXT=${PRIMO_MODEL_SOLVER_ARGS:-}
SOLVER_TIMEOUT=${PRIMO_MODEL_SOLVER_TIMEOUT:-60}
DOLMEN=${PRIMO_MODEL_DOLMEN:-dolmen}
DOLMEN_TIMEOUT=${PRIMO_MODEL_DOLMEN_TIMEOUT:-30}
DOLMEN_ARGS_TEXT=${PRIMO_MODEL_DOLMEN_ARGS---strict=false}
Z3=${PRIMO_MODEL_Z3:-z3}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"
PATH="$WORKDIR/contrib/local/bin:$HOME/.opam/default/bin:$PATH"
export PATH

if ((TASK_ID >= SHARDS)); then
  echo "task $TASK_ID is outside the $SHARDS-shard range"
  exit 0
fi
if [[ ! -f $MANIFEST ]]; then
  echo "model-validation manifest not found: $MANIFEST" >&2
  exit 2
fi
if [[ ! -d $ROOT ]]; then
  echo "QF_UFLRA benchmark root not found: $ROOT" >&2
  exit 2
fi
for required in "$SOLVER" "$DOLMEN" "$Z3"; do
  if ! command -v "$required" >/dev/null 2>&1 && [[ ! -x $required ]]; then
    echo "required executable '$required' not found" >&2
    exit 2
  fi
done

solver_args=(--solver-arg=--quiet)
if [[ -n $SOLVER_ARGS_TEXT ]]; then
  read -r -a solver_arg_words <<< "$SOLVER_ARGS_TEXT"
  for arg in "${solver_arg_words[@]}"; do
    solver_args+=(--solver-arg="$arg")
  done
fi
dolmen_args=()
if [[ -n $DOLMEN_ARGS_TEXT ]]; then
  read -r -a dolmen_arg_words <<< "$DOLMEN_ARGS_TEXT"
  for arg in "${dolmen_arg_words[@]}"; do
    dolmen_args+=(--dolmen-arg="$arg")
  done
fi

"$SOLVER" --version
"$DOLMEN" --version

python3 scripts/validate_model.py \
  --solver "$SOLVER" \
  "${solver_args[@]}" \
  --dolmen "$DOLMEN" \
  "${dolmen_args[@]}" \
  --z3 "$Z3" \
  --require-dolmen-pass \
  --timeout "$SOLVER_TIMEOUT" \
  --dolmen-timeout "$DOLMEN_TIMEOUT" \
  --manifest "$MANIFEST" \
  --root "$ROOT" \
  --shards "$SHARDS" \
  --shard-index "$TASK_ID" \
  --out "$OUT_ROOT/shard-$TASK_ID"
