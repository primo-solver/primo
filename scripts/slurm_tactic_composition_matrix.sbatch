#!/usr/bin/env bash
# Merge-grade matrix for legal production tactic compositions. Four shards per
# logic generate 250 unique cases each, then run every profile applicable to
# that logic. Every satisfiable profile/case pair is checked directly by Dolmen.
#SBATCH --job-name=primo-tactic-matrix
#SBATCH --array=0-19
#SBATCH --cpus-per-task=1
#SBATCH --mem=3G
#SBATCH --time=04:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:?PRIMO_WORKDIR must name the prepared remote workspace}
SOLVER=${PRIMO_FUZZ_SOLVER:-build-slurm-rel/primo}
Z3=${PRIMO_FUZZ_Z3:-contrib/local/bin/z3}
CVC5=${PRIMO_FUZZ_CVC5:-contrib/local/bin/cvc5}
DOLMEN=${PRIMO_FUZZ_DOLMEN:-dolmen}
FUZZSMT=${PRIMO_FUZZ_FUZZSMT:-contrib/local/bin/fuzzsmt}
COUNT=${PRIMO_FUZZ_COUNT:-250}
TIMEOUT=${PRIMO_FUZZ_TIMEOUT:-10}
SEED_BASE=${PRIMO_FUZZ_SEED_BASE:-202609010}
TASK_ID=${SLURM_ARRAY_TASK_ID:?SLURM_ARRAY_TASK_ID is required}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/tactic-composition-${SLURM_ARRAY_JOB_ID:-manual}"}

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"
PATH="$WORKDIR/contrib/local/bin:$HOME/.opam/default/bin:$PATH"
export PATH

for required in "$SOLVER" "$Z3" "$CVC5" "$DOLMEN"; do
  if ! command -v "$required" >/dev/null 2>&1 && [[ ! -x $required ]]; then
    echo "required gate tool '$required' not found" >&2
    exit 2
  fi
done

logic_index=$((TASK_ID / 4))
shard=$((TASK_ID % 4))
seed=$((SEED_BASE + TASK_ID))
task_out="$OUT_ROOT/task-$TASK_ID"
generator_out="$task_out/generator"
case_list="$task_out/cases.txt"
mkdir -p "$generator_out"

case "$logic_index" in
  0)
    logic=QF_UF
    python3 scripts/qf_uf_boolean_fuzz.py \
      --solver "$SOLVER" --solver-arg=--quiet --ref "$Z3" \
      --count "$COUNT" --seed "$seed" --timeout "$TIMEOUT" \
      --shape all --keep-passing --out "$generator_out"
    find "$generator_out/cases" -type f -name '*.smt2' | sort > "$case_list"
    ;;
  1)
    logic=QF_LRA
    python3 scripts/qf_lra_boolean_arithmetic_fuzz.py \
      --solver "$SOLVER" --solver-arg=--quiet --z3 "$Z3" --dolmen "$DOLMEN" \
      --count "$COUNT" --seed "$seed" --timeout "$TIMEOUT" \
      --dolmen-timeout "$TIMEOUT" --out "$generator_out"
    find "$generator_out/cases" -type f -name '*.smt2' | sort > "$case_list"
    ;;
  2)
    logic=QF_UFLRA
    python3 scripts/qf_uflra_smoke_fuzz.py \
      --solver "$SOLVER" --solver-arg=--quiet --ref "$Z3" \
      --count "$COUNT" --seed "$seed" --timeout "$TIMEOUT" \
      --shape mixed --out "$generator_out"
    find "$generator_out/cases" -type f -name '*.smt2' | sort > "$case_list"
    ;;
  3)
    logic=QF_LIA
    if ! command -v "$FUZZSMT" >/dev/null 2>&1 && [[ ! -x $FUZZSMT ]]; then
      echo "required QF_LIA generator '$FUZZSMT' not found" >&2
      exit 2
    fi
    python3 scripts/fuzz_external.py \
      --fuzzer fuzzsmt --solver "$SOLVER --quiet" --refs "$Z3" \
      --logic QF_LIA --count "$COUNT" --seed "$seed" --timeout "$TIMEOUT" \
      --fuzzer-args '-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4' \
      --out "$generator_out"
    find "$generator_out/generated" -type f -name '*.smt2' | sort > "$case_list"
    ;;
  4)
    logic=QF_UFLIA
    python3 scripts/qf_uflia_fuzz.py \
      --solver "$SOLVER" --solver-arg=--quiet --ref "$Z3" --ref "$CVC5" \
      --count "$COUNT" --seed "$seed" --timeout "$TIMEOUT" \
      --shape mixed --out "$generator_out"
    find "$generator_out/cases" -type f -name '*.smt2' | sort > "$case_list"
    ;;
  *)
    echo "task $TASK_ID has no logic mapping" >&2
    exit 2
    ;;
esac

actual_count=$(wc -l < "$case_list")
if [[ $actual_count != "$COUNT" ]]; then
  echo "$logic shard $shard expected $COUNT generated cases, found $actual_count" >&2
  exit 2
fi
if [[ $logic == QF_UF ]]; then
  # Added after the generated-count assertion: this tracked coverage seed
  # guarantees that every QF_UF shard exercises adaptive continuation.
  printf '%s\n' "$WORKDIR/benchmarks/seeds/qf_uf/qg_symmetry_residual_domain.smt2" >> "$case_list"
fi

"$SOLVER" --version
echo "logic=$logic shard=$shard seed=$seed count=$COUNT"
python3 scripts/tactic_composition_matrix.py \
  --manifest benchmarks/tactic-composition-matrix.json \
  --case-list "$case_list" --logic "$logic" --root "$WORKDIR" \
  --solver "$SOLVER" --refs "$Z3" "$CVC5" --dolmen "$DOLMEN" \
  --timeout "$TIMEOUT" --dolmen-timeout "$TIMEOUT" --repeat 1 \
  --allow-inconclusive --out "$task_out/matrix"
