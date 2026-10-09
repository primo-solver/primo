#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-default-precheck
#SBATCH --partition=compute
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
mkdir -p results/lra-default-candidate-20261009/precheck
build-slurm-rel/primo --version | tee results/lra-default-candidate-20261009/precheck/version.txt
grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}" results/lra-default-candidate-20261009/precheck/version.txt
/home/janotmik/primo-cadical-profile/.venv-build/bin/cmake -E chdir build-slurm-rel \
  /home/janotmik/primo-cadical-profile/.venv-build/bin/ctest \
  --output-on-failure -R 'primo_(qf_lra_solver_tests|boolean_solver_tests|lra_heuristic_cli)$'
