#!/usr/bin/env bash
# Reuse the qualified workspace's existing test executables; no login-host build.
#SBATCH --job-name=primo-soi-audit-checks
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:05:00
#SBATCH --hint=nomultithread
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
out="${PRIMO_SOI_OUT:?}/checks-retry"
mkdir -p "$out"
sha256sum build-slurm-rel/primo_lra_tableau_tests \
  build-slurm-rel/primo_qf_lra_solver_tests > "$out/binaries.sha256"
cp build-slurm-rel/CMakeFiles/primo_lra_tableau_tests.dir/flags.make "$out/tableau-flags.make"
cp build-slurm-rel/CMakeFiles/primo_qf_lra_solver_tests.dir/flags.make "$out/engine-flags.make"
for target in primo_lra_tableau_tests primo_qf_lra_solver_tests; do
  "build-slurm-rel/$target" > "$out/$target.stdout" 2> "$out/$target.stderr"
  printf '%s PASS\n' "$target" | tee -a "$out/results.txt"
done
