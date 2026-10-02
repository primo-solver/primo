#!/usr/bin/env bash
# Eight advertised routes, four independently seeded 250-input shards each.
# Submit only after slurm_prepare_benchmark_workspace.py --check-only succeeds.
#SBATCH --job-name=primo-public-gate
#SBATCH --array=0-31%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A_%a.out
#SBATCH --error=logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
slot=${SLURM_ARRAY_TASK_ID:?}
expected=${PRIMO_EXPECTED_REVISION:?}
solver=build-slurm-rel/primo
root="results/public-launch/${SLURM_ARRAY_JOB_ID}/shard-$slot"
test ! -e "$root"
mkdir -p "$root"
exec > >(tee "$root/stdout.log") 2> >(tee "$root/stderr.log" >&2)
trap 'status=$?; printf "%s\n" "$status" > "$root/exit-code"' EXIT
set -x
"$solver" --version > "$root/version.txt"
grep -Fx "git: $expected" "$root/version.txt"
sha256sum "$solver" contrib/local/bin/z3 contrib/local/bin/dolmen > "$root/executables.sha256"
z3 --version > "$root/reference-version.txt"
dolmen --version > "$root/validator-version.txt"
seed=$((202610020 + slot))
out="$root/gate"
common=(--solver "$solver" --count 250 --seed "$seed" --timeout 5 --out "$out")
case $((slot / 4)) in
  0)
    python3 scripts/qf_uf_boolean_fuzz.py "${common[@]}" --ref z3 \
      --solver-arg=--quiet --keep-passing --validate-models --dolmen dolmen
    ;;
  1)
    python3 scripts/qf_lra_boolean_arithmetic_fuzz.py "${common[@]}" \
      --solver-arg=--quiet --z3 z3 --dolmen dolmen
    ;;
  2)
    command -v fuzzsmt
    python3 scripts/fuzz_external.py --fuzzer fuzzsmt \
      --solver "$solver --quiet" --refs z3 --logic QF_LIA --count 250 \
      --seed "$seed" --timeout 5 --out "$out" \
      --fuzzer-args '-ms 1 -Ms 4 -mv 1 -Mv 6 -mf 1 -mp 1 -ma 1 -Ma 4'
    python3 scripts/validate_model.py --solver "$solver" --solver-arg=--quiet \
      --require-dolmen-pass --dolmen dolmen --dolmen-arg=--strict=false \
      --timeout 10 --dolmen-timeout 10 --out "$root/models" "$out"/generated/*.smt2
    ;;
  3|4)
    if (( slot / 4 == 3 )); then gate=qf_uflra_smoke_fuzz; else gate=qf_uflia_fuzz; fi
    python3 "scripts/$gate.py" "${common[@]}" --solver-arg=--quiet --ref z3 --shape mixed
    python3 scripts/select_fuzz_sat_inputs.py "$out/summary.csv" --out "$root/sat-inputs.txt"
    if [[ -s "$root/sat-inputs.txt" ]]; then
      python3 scripts/validate_model.py --solver "$solver" --solver-arg=--quiet \
        --require-dolmen-pass --expect-sat --dolmen dolmen --dolmen-arg=--strict=false \
        --timeout 10 --dolmen-timeout 10 --manifest "$root/sat-inputs.txt" --out "$root/models"
    fi
    ;;
  5)
    python3 scripts/quantifier_preparation_gate.py "${common[@]}" --mode generated \
      --ref z3 --expected-git "$expected" --check-preprocess-only
    ;;
  6|7)
    if (( slot / 4 == 6 )); then gate=incremental_uf_gate; else gate=incremental_lra_gate; fi
    python3 "scripts/$gate.py" "${common[@]}" --kind generated --ref z3 --dolmen dolmen \
      --solver-arg=--quiet --solver-arg=--incremental-mode=assumptions \
      --expected-git "$expected" --dolmen-timeout 20
    ;;
  *) exit 2 ;;
esac
