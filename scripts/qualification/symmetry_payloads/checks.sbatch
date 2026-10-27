#!/usr/bin/env bash
#SBATCH --job-name=primo-symmetry-checks
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:40:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { echo "SLURM allocation required" >&2; exit 2; }
cd "${PRIMO_WORKDIR:?}"
root=results/symmetry-checks
mkdir -p "$root"
exec > >(tee "$root/stdout.log") 2> >(tee "$root/stderr.log" >&2)
trap 'status=$?; printf "%s\n" "$status" > "$root/exit-code"' EXIT
export PATH="${PRIMO_CMAKE_BIN:?}:$PWD/contrib/local/clang-tools-env/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4 CTEST_TEST_TIMEOUT=120
build-slurm-rel/primo --version | tee "$root/version.txt"
sha256sum build-slurm-rel/primo contrib/local/bin/z3 contrib/local/bin/cvc5 contrib/local/bin/dolmen > "$root/sha256.txt"
ctest --test-dir build-slurm-rel --timeout 120 --output-on-failure -R 'primo_(preprocessing|boolean_solver)_tests' > "$root/focused.txt" 2>&1
for profile in default basic-zero basic-zero-no-precedence symmetry-off; do
  options=()
  case "$profile" in
    default) ;;
    basic-zero) options=(--preprocessing-mode=basic --preprocessing-passes=0) ;;
    basic-zero-no-precedence) options=(--preprocessing-mode=basic --preprocessing-passes=0 --no-finite-domain-value-precedence) ;;
    symmetry-off) options=(--preprocessing-mode=basic --preprocessing-passes=0 --no-uf-symmetry-breaking) ;;
  esac
  args=()
  for option in "${options[@]}"; do args+=(--solver-arg="$option"); done
  python3 scripts/validate_model.py --solver build-slurm-rel/primo "${args[@]}" \
    --solver-arg=--quiet --require-dolmen-pass --expect-sat \
    --dolmen contrib/local/bin/dolmen --dolmen-arg=--strict=false \
    --timeout 10 --dolmen-timeout 10 --out "$root/models-$profile" \
    benchmarks/smoke/qf_uflra/sat_symmetry_*.smt2
 done
# These are correctness checks. No timing/performance claim is made.
bash scripts/qualification/public_checks.sbatch
