#!/usr/bin/env bash
#SBATCH --job-name=primo-uf-session-tests
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --output=slurm_logs/%x-%j.out
#SBATCH --error=slurm_logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
python3 -m unittest discover -s tests/python -p test_incremental_uf_gate.py
python3 -m unittest discover -s tests/python -p test_native_uf_summary.py
"${PRIMO_CTEST:?}" --test-dir build-slurm-rel \
  -R 'primo_(smt_session_tests|sat_backend_tests|euf_solver_tests|assumptions.*cli)' \
  --timeout 120 --output-on-failure
python3 scripts/validate_incremental.py benchmarks/seeds/qf_uf/seed_bool_argument_*.smt2 \
  bugs/qf-uf-incremental-ite-model/witness.smt2 \
  bugs/qf-uf-incremental-ite-model/generated-witness.smt2 \
  --solver build-slurm-rel/primo --solver-arg=--quiet \
  --solver-arg=--incremental-mode=assumptions --solver-arg=--max-theory-conflicts=10000 \
  --ref contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
  --out "${PRIMO_INCREMENTAL_OUT:?}/boolean-seeds"
