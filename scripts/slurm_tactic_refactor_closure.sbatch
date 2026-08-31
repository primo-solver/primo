#!/usr/bin/env bash
# Repeat timeout-boundary and noisy-family comparisons needed to close the
# immutable-tactic refactor's paired 3120d54 versus 618720d evaluation.
#SBATCH --job-name=primo-tactic-closure
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=04:00:00
#SBATCH --array=0-65
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

QFLIA_WORKDIR=${PRIMO_QFLIA_WORKDIR:?PRIMO_QFLIA_WORKDIR is required}
QFLRA_WORKDIR=${PRIMO_QFLRA_WORKDIR:?PRIMO_QFLRA_WORKDIR is required}
BASELINE_SOLVER=${PRIMO_BASELINE_SOLVER:?PRIMO_BASELINE_SOLVER is required}
CANDIDATE_SOLVER=${PRIMO_CANDIDATE_SOLVER:?PRIMO_CANDIDATE_SOLVER is required}
BENCH_SCRIPT=${PRIMO_BENCH_SCRIPT:?PRIMO_BENCH_SCRIPT is required}
BOUNDARY_MANIFEST=${PRIMO_BOUNDARY_MANIFEST:?PRIMO_BOUNDARY_MANIFEST is required}
OUT_ROOT=${PRIMO_COMPARE_OUT_ROOT:?PRIMO_COMPARE_OUT_ROOT is required}
TASK_ID=${SLURM_ARRAY_TASK_ID:?SLURM_ARRAY_TASK_ID is required}

manifest=()
if ((TASK_ID < 30)); then
  repeat=$((TASK_ID % 6))
  case $((TASK_ID / 6)) in
    0)
      label=qflia-boundary
      corpus_workdir=$QFLIA_WORKDIR
      corpus_root=sandbox/non-incremental/QF_LIA
      expected_count=15
      timeout=60
      manifest=(--manifest "$BOUNDARY_MANIFEST")
      ;;
    1)
      label=qflia-check
      corpus_workdir=$QFLIA_WORKDIR
      corpus_root=sandbox/non-incremental/QF_LIA/check
      expected_count=5
      timeout=60
      ;;
    2)
      label=qflia-cut_lemmas
      corpus_workdir=$QFLIA_WORKDIR
      corpus_root=sandbox/non-incremental/QF_LIA/cut_lemmas
      expected_count=93
      timeout=60
      ;;
    3)
      label=qflia-prime-cone
      corpus_workdir=$QFLIA_WORKDIR
      corpus_root=sandbox/non-incremental/QF_LIA/prime-cone
      expected_count=37
      timeout=60
      ;;
    4)
      label=qflia-tightrhombus
      corpus_workdir=$QFLIA_WORKDIR
      corpus_root=sandbox/non-incremental/QF_LIA/tightrhombus
      expected_count=22
      timeout=60
      ;;
    *)
      echo "unexpected task $TASK_ID" >&2
      exit 2
      ;;
  esac
elif ((TASK_ID < 48)); then
  label=qflra-keymaera
  corpus_workdir=$QFLRA_WORKDIR
  corpus_root=tmp/QF_LRA/keymaera
  expected_count=21
  timeout=120
  repeat=$((TASK_ID - 24))
else
  label=qflia-prime-cone
  corpus_workdir=$QFLIA_WORKDIR
  corpus_root=sandbox/non-incremental/QF_LIA/prime-cone
  expected_count=37
  timeout=60
  repeat=$((TASK_ID - 42))
fi

mkdir -p logs "$OUT_ROOT/$label/repeat-$repeat/baseline" \
  "$OUT_ROOT/$label/repeat-$repeat/candidate"

baseline_version=$($BASELINE_SOLVER --version)
candidate_version=$($CANDIDATE_SOLVER --version)
printf '%s\n' "$baseline_version"
printf '%s\n' "$candidate_version"
grep -F 'git: 3120d54' <<<"$baseline_version"
grep -F 'git: 618720d' <<<"$candidate_version"
baseline_cadical=$(grep '^cadical:' <<<"$baseline_version")
candidate_cadical=$(grep '^cadical:' <<<"$candidate_version")
if [[ $baseline_cadical != "$candidate_cadical" ]]; then
  echo "CaDiCaL mismatch: $baseline_cadical vs $candidate_cadical" >&2
  exit 2
fi

if ((${#manifest[@]} == 0)); then
  actual_count=$(find "$corpus_workdir/$corpus_root" -type f -name '*.smt2' | wc -l)
else
  actual_count=$(grep -Ev '^[[:space:]]*(#|$)' "$BOUNDARY_MANIFEST" | wc -l)
fi
if [[ $actual_count != "$expected_count" ]]; then
  echo "$label expected $expected_count inputs, found $actual_count" >&2
  exit 2
fi

run_arm() {
  local arm=$1
  local solver=$2
  python3 "$BENCH_SCRIPT" \
    --repo "$corpus_workdir" \
    --root "$corpus_root" \
    "${manifest[@]}" \
    --solver "$solver" \
    --solver-arg=--quiet \
    --timeout "$timeout" \
    --shards 1 \
    --shard-index 0 \
    --out "$OUT_ROOT/$label/repeat-$repeat/$arm/results.csv" \
    --summary "$OUT_ROOT/$label/repeat-$repeat/$arm/summary.csv" \
    --log-dir "$OUT_ROOT/$label/repeat-$repeat/$arm/logs"
}

if ((repeat % 2 == 0)); then
  run_arm baseline "$BASELINE_SOLVER"
  run_arm candidate "$CANDIDATE_SOLVER"
else
  run_arm candidate "$CANDIDATE_SOLVER"
  run_arm baseline "$BASELINE_SOLVER"
fi
