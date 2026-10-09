#!/usr/bin/env bash
# Paired QF_LRA timings and separate SAT-model replay for four Primo profiles.
# Array tasks encode profile_index * PRIMO_SHARDS + shard_index. Profiles 4 and
# 5 optionally add OpenSMT and Yices. Profile 6 measures the automatic default.
# New binaries use explicit opt-outs for the original four ablation profiles;
# archived default-off binaries retain their original supported arguments.
#SBATCH --job-name=primo-lra-profiles
#SBATCH --cpus-per-task=1
#SBATCH --mem=7G
#SBATCH --time=03:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
export PATH="$PWD/contrib/local/bin:$PATH"
SHARDS=${PRIMO_SHARDS:?}
TASK=${SLURM_ARRAY_TASK_ID:-0}
PROFILE=$((TASK / SHARDS))
SHARD=$((TASK % SHARDS))
PHASE=${PRIMO_PHASE:?benchmark or replay}
args=()
case "$PROFILE" in
  0) name=default; solver=build-slurm-rel/primo; args=(--quiet) ;;
  1) name=model-phase; solver=build-slurm-rel/primo; args=(--quiet --lra-model-phase) ;;
  2) name=monotone; solver=build-slurm-rel/primo; args=(--quiet --monotone-elimination) ;;
  3) name=both; solver=build-slurm-rel/primo; args=(--quiet --lra-model-phase --monotone-elimination) ;;
  4) name=opensmt; solver=${PRIMO_OPENSMT:-contrib/local/bin/opensmt} ;;
  5) name=yices; solver=${PRIMO_YICES:-contrib/local/bin/yices-smt2} ;;
  6) name=default; solver=build-slurm-rel/primo; args=(--quiet) ;;
  *) echo "invalid profile index $PROFILE" >&2; exit 2 ;;
esac
if (( PROFILE < 4 || PROFILE == 6 )); then
  help=$("$solver" --help)
  if [[ $help == *--no-lra-model-phase* ]]; then defaults=automatic; else defaults=off; fi
  case "$defaults" in
    automatic)
      case "$PROFILE" in
        0) name=legacy-default; args+=(--no-lra-model-phase --no-monotone-elimination) ;;
        1) args+=(--no-monotone-elimination) ;;
        2) args+=(--no-lra-model-phase) ;;
      esac
      ;;
    off)
      if (( PROFILE == 6 )); then echo 'profile 6 requires the automatic-policy binary' >&2; exit 2; fi
      ;;
  esac
fi
out="${PRIMO_CAMPAIGN_OUT:?}/$name/shard-$SHARD"
mkdir -p logs "$out"
command=(python3 scripts/benchmark_model_replay.py "$PHASE" --out "$out")
case "$PHASE" in
  benchmark)
    command+=(--root "${PRIMO_CORPUS_ROOT:?}" --solver "$solver"
              --run-id "${PRIMO_CAMPAIGN_ID:?}-$name" --shards "$SHARDS"
              --shard-index "$SHARD" --timeout "${PRIMO_TIMEOUT:?}")
    if [[ -n ${PRIMO_MANIFEST:-} ]]; then command+=(--manifest "$PRIMO_MANIFEST"); fi
    if (( PROFILE < 4 || PROFILE == 6 )); then command+=(--expected-git "${PRIMO_EXPECTED_GIT:?}"); fi
    for arg in "${args[@]}"; do command+=(--solver-arg="$arg"); done
    ;;
  replay)
    if (( PROFILE == 4 || PROFILE == 5 )); then echo 'reference profiles have no Primo model replay' >&2; exit 2; fi
    command+=(--model-timeout "${PRIMO_MODEL_TIMEOUT:?}"
              --dolmen-timeout "${PRIMO_DOLMEN_TIMEOUT:-60}" --dolmen-size 6G
              --dolmen-arg=--strict=false --attempt "${PRIMO_REPLAY_ATTEMPT:-model-validation}")
    ;;
  *) echo "invalid phase $PHASE" >&2; exit 2 ;;
esac
"${command[@]}"
