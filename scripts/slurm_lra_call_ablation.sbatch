#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-calls
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=00:20:00
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="${PRIMO_CMAKE_DIR:?}:$PWD/contrib/local/bin:$PATH"
export PYTHONPATH="$PWD/scripts"
OUT=${PRIMO_CALL_OUT:?}
mkdir -p "$OUT"
build-slurm-rel/primo --version | tee "$OUT/version-${SLURM_ARRAY_TASK_ID:-single}.txt"
build-slurm-rel/primo --version | grep -Fx "git: ${PRIMO_EXPECTED_GIT:?}"
lscpu | sed -n '/Model name/p;/^CPU(s):/p'
case "${PRIMO_CALL_PHASE:?}" in
 check)
 ctest --test-dir build-slurm-rel --timeout 120 --output-on-failure
 python3 -m unittest discover -s tests/python -p 'test_*.py'
 python3 scripts/check_doc_examples.py --solver build-slurm-rel/primo
 ;;
 fuzz)
 python3 scripts/qf_lra_boolean_arithmetic_fuzz.py --solver build-slurm-rel/primo \
  --solver-arg=--lra-call-profile --solver-arg="$OUT/current-profile.jsonl" \
  --z3 contrib/local/bin/z3 --dolmen contrib/local/bin/dolmen \
  --count 1000 --seed 2026103001 --timeout 5 --dolmen-timeout 10 --out "$OUT/fuzz"
 ;;
 asan)
 probe_cxxflags=""
 while IFS= read -r header; do probe_cxxflags="$probe_cxxflags -isystem $header"; done < tmp/cxx-header-paths.txt
 export CXXFLAGS="$probe_cxxflags"
 export CMAKE_BUILD_PARALLEL_LEVEL=${SLURM_CPUS_PER_TASK:-4}
 export LD_LIBRARY_PATH="$PWD/tmp/asan-runtime${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
 cmake -S . -B build-call-asan -DCMAKE_BUILD_TYPE=Debug -DPRIMO_SANITIZERS=address \
  "-DCMAKE_PREFIX_PATH=$PWD/contrib/local" "-DPRIMO_GIT_REVISION_OVERRIDE=$PRIMO_EXPECTED_GIT" \
  "-DCMAKE_EXE_LINKER_FLAGS=-L$PWD/tmp/asan-runtime"
 make -o contrib-required asan-test ASAN_BUILD_DIR=build-call-asan
 ;;
 models)
 manifest="${PRIMO_CALL_PARENT_OUT:?}/shard-${SLURM_ARRAY_TASK_ID}/parents-sat.txt"
 if [[ -s $manifest ]]; then
  python3 scripts/validate_model.py --solver build-slurm-rel/primo --expect-sat \
   --require-dolmen-pass --dolmen contrib/local/bin/dolmen --dolmen-size 2G \
   --dolmen-arg=--strict=false --timeout 60 --dolmen-timeout 30 \
   --solver-arg=--quiet --solver-arg=--stats \
   --solver-arg=--lra-call-profile --solver-arg="$OUT/replay-profile-${SLURM_ARRAY_TASK_ID}.jsonl" \
   --manifest "$manifest" --out "$OUT/shard-${SLURM_ARRAY_TASK_ID}"
 fi
 ;;
 profile|extract)
 arguments=()
 if [[ $PRIMO_CALL_PHASE == extract ]]; then
  arguments+=(--export-min-pivots "${PRIMO_CALL_MIN_PIVOTS:?}" --export-limit 5)
 fi
 python3 "${PRIMO_CALL_JOB_ARTIFACTS:?}/run_lra_call_distribution.py" \
  --solver build-slurm-rel/primo --reference contrib/local/bin/z3 \
  --root tmp/QF_LRA --manifest "${PRIMO_CALL_MANIFEST:?}" \
  --timeout "${PRIMO_CALL_TIMEOUT:-5}" --shards "${PRIMO_CALL_SHARDS:-8}" \
  --index "${SLURM_ARRAY_TASK_ID:-0}" --out "$OUT/shard-${SLURM_ARRAY_TASK_ID:-0}" \
  "${arguments[@]}"
 ;;
 *) exit 2 ;;
esac
