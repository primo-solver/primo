#!/usr/bin/env bash
# Fresh default-profile real-seed operator mutation. This harness alone is
# insufficient evidence: replay every retained input with exit-status checks.
#SBATCH --job-name=primo-lra-default-mutation
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:45:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PWD/.venv-yinyang/bin:$PATH"
out=${PRIMO_CAMPAIGN_OUT:?}
mkdir -p "$out" logs
build-slurm-rel/primo --version > "$out/version.txt"
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" "$out/version.txt"
sha256sum build-slurm-rel/primo > "$out/binary.sha256"
python3 scripts/build_fuzz_seed_corpus.py --root "${PRIMO_CORPUS_ROOT:?}" \
  --out "$out/seeds" --per-family 2 --max-bytes 200000
for regression in qf_lra qf_lra_real; do
  python3 scripts/check_solver_seed_corpus.py --seeds "benchmarks/seeds/$regression" \
    --solver build-slurm-rel/primo --solver-arg=--quiet \
    --ref contrib/local/bin/z3 --timeout 30 --out "$out/regression-$regression.csv"
done
scripts/yinyang_fuzz.sh --seeds "$out/seeds" \
  --solver 'build-slurm-rel/primo --quiet' --ref scripts/yices_smt2_ref.sh \
  --typefuzz "$PWD/scripts/seeded_opfuzz.py" --iterations 200 \
  --config benchmarks/fuzz-configs/qf_lra_operator_mutations.txt \
  --seconds 1500 --solver-timeout 5 --file-size-limit 200000 \
  --keep-logic --keep-mutants --random-seed 2026101300 \
  --bugs "$out/bugs" --scratch "$out/scratch" --logs "$out/logs"
