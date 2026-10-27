#!/usr/bin/env bash
#SBATCH --job-name=primo-shared-models
#SBATCH --array=4-5%2
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
[[ -n ${SLURM_JOB_ID:-} ]]
cd "${PRIMO_WORKDIR:?}"
./build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
input=${PRIMO_MIXED_RETRY_OUT:?}/task-$SLURM_ARRAY_TASK_ID
out=${PRIMO_MIXED_MODEL_OUT:?}/task-$SLURM_ARRAY_TASK_ID
mkdir -p "$out"
flags=(--solver-arg=--quiet)
if (( SLURM_ARRAY_TASK_ID == 5 )); then
 flags+=(--solver-arg=--qf-uflra-online-euf --solver-arg=--euf-prop=online)
fi
export PATH="$PWD/contrib/local/bin:$PATH"
for corpus in general finite-qf-uflra finite-qf-uf; do
 python3 scripts/select_fuzz_sat_inputs.py "$input/$corpus/summary.csv" --out "$out/$corpus-sat.txt"
 extra=()
 if [[ $corpus == finite-qf-uf ]]; then extra+=(--solver-arg=--preprocessing-mode=strong); fi
 if [[ ! -s $out/$corpus-sat.txt ]]; then continue; fi
 python3 scripts/validate_model.py --solver build-slurm-rel/primo "${flags[@]}" "${extra[@]}" \
  --expect-sat --require-dolmen-pass --dolmen-arg=--strict=false \
  --timeout 10 --dolmen-timeout 10 --manifest "$out/$corpus-sat.txt" --out "$out/$corpus"
done
