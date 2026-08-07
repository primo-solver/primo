#!/usr/bin/env bash
#SBATCH --job-name=primo-qfuf-fuzz-gate
#SBATCH --array=0-8
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=02:00:00
#SBATCH --output=logs/%x-%A-%a.out
#SBATCH --error=logs/%x-%A-%a.err

# Wall limit. Two separate things to know here.
#
# 1. A shard that hits the wall is killed *before* fuzz_external.py writes its
#    summary.csv, so the shard vanishes from the results instead of failing:
#    the gate reports fewer cases and otherwise looks perfectly clean. Jobs
#    117248-117251 each lost exactly one shard that way (2500 of 3000 cases)
#    and read as 0 crashes / 0 wrong answers. Always check that the number of
#    summary.csv files equals the shard count before believing a gate result;
#    scripts/summarize_fuzz_results.py prints it for this reason.
#
# 2. The bound itself: a shard is FUZZSMT_COUNT cases, each costing up to
#    FUZZSMT_TIMEOUT for our solver plus the same for EACH reference, so the
#    worst case is roughly COUNT * (1 + refs) * TIMEOUT -- with QF_LRA's two
#    references that is 500 * 3 * 10s = 4.2 hours, not the 83 minutes the
#    single-reference arithmetic gave. Raise this if you raise any of them.
#
#    Generation is not free either, and its cost is NOT a function of the
#    parameters alone. Measured 2026-08-02 at the identical profile
#    `-mv 1 -Mv 3 -mc 1 -Mc 3 -Mbw 4`, 100 files each:
#
#      seed 218735   20.9s   34.7 MB      <-- ~200x slower, ~20x bigger
#      seed 218736    0.1s    1.9 MB
#      seed 218740    0.1s    1.4 MB
#      seed 12345     0.3s    3.8 MB
#
#    FuzzSMT's output size is a property of its random stream, so a seed can
#    emit ~347 KB formulas where its neighbours emit ~14 KB ones. A shard that
#    draws such a seed does far more work than its siblings at every stage --
#    generating, solving, and referencing -- and looks stuck while being
#    merely slow. Job 118735's shard 0 sat at an hour while shards 1-5
#    finished in 1-8 minutes, purely from this.
#
# That arithmetic was NOT what caused the 117248-117251 losses, though, and
# the investigation is worth not repeating: all four lost shards were the same
# FuzzSMT seed (217251) across four different solver configurations, while the
# one job whose seeds skipped it completed every shard. That looks exactly like
# a solver hang on one input, and is not: the same seed completes 500/500
# locally in ~40s, on a compute node writing to this same NFS tree in 126s, and
# again to node-local /tmp; generation of its 500 files takes 0.6s remotely;
# peak RSS is ~95MB against the 2G cgroup. The failures happened while the
# cluster had 460+ queued jobs from other users and did not reproduce once it
# drained. Treat a whole-shard timeout as an environment signal first, but
# check the seed pattern -- if one seed fails across configurations *and*
# reproduces on a quiet cluster, that would be the real thing.
#
# Since 2026-08-02 there is a second explanation to rule out before blaming
# the environment, and it is cheap to check: regenerate that shard's seed
# locally and time it (see the generation-cost note above). A seed that is
# pathologically expensive to generate needs no cluster to reproduce.
#
# Combined QF_UF fuzz gate: FuzzSMT (generator) + opfuzz + YinYang typefuzz
# (mutation), one array job. This is the campaign previously reconstructed
# by hand each time from docs/run-log.json notes (e.g. 2026-07-18 entries
# qfuf-fuzzsmt-yinyang-5de60d9 / -6f3cdca); it is now a checked-in script so
# it does not need to be re-derived.
#
# Task layout (defaults: 6 FuzzSMT shards + 1 opfuzz task + 2 YinYang shards
# = 9 tasks, array 0-8):
#   [0 .. FUZZSMT_SHARDS-1]                         FuzzSMT via fuzz_external.py
#   [FUZZSMT_SHARDS .. +OPFUZZ_TASKS-1]              opfuzz via yinyang_fuzz.sh --typefuzz opfuzz
#   [.. +YINYANG_SHARDS-1]                           YinYang typefuzz via yinyang_fuzz.sh
#
# If you override the *_SHARDS/*_TASKS env vars, also pass a matching
# --array=0-N to sbatch (SBATCH directives are static and cannot read env).
#
# Validate before submitting: python3 scripts/slurm_validate_sbatch.py \
#   scripts/slurm_qfuf_fuzz_gate.sbatch
#
# Submit: sbatch scripts/slurm_qfuf_fuzz_gate.sbatch
# Rerun with a fresh FuzzSMT corpus and same layout: just submit again
# (the seed base is derived from SLURM_ARRAY_JOB_ID by default).

set -euo pipefail

WORKDIR=${PRIMO_WORKDIR:-$PWD}
SOLVER_BIN=${PRIMO_FUZZ_SOLVER:-"$WORKDIR/build-slurm-fuzz/primo"}
OUT_ROOT=${PRIMO_FUZZ_OUT_ROOT:-"$WORKDIR/fuzz-results/qfuf-gate-${SLURM_ARRAY_JOB_ID:-manual}"}
TASK_ID=${SLURM_ARRAY_TASK_ID:-0}

FUZZSMT_SHARDS=${PRIMO_FUZZ_FUZZSMT_SHARDS:-6}
FUZZSMT_COUNT=${PRIMO_FUZZ_FUZZSMT_COUNT:-500}
FUZZSMT_TIMEOUT=${PRIMO_FUZZ_FUZZSMT_TIMEOUT:-5}
FUZZSMT_LOGIC=${PRIMO_FUZZ_FUZZSMT_LOGIC:-QF_UF}
# Two references for QF_LRA, one for QF_UF. Yices 2 earns its place on
# measurements, not on principle (2026-08-01, seeds of the 17-family corpus):
# it answers latendresse and miplib_pp08a-9000 where z3 has no answer at 60s,
# and on generator output it agreed with z3 on all 150 wide-coefficient cases
# while running 6x faster, so it is also the cheaper reference. It does not
# subsume z3 -- both time out on LassoRanker and tropical-matrix -- so this is
# an addition, and it buys reference cross-checking (classify() reports
# REF_DISAGREEMENT) for free.
if [[ $FUZZSMT_LOGIC == QF_LRA ]]; then
  FUZZSMT_REF=${PRIMO_FUZZ_FUZZSMT_REF:-"z3 yices-smt2"}
else
  FUZZSMT_REF=${PRIMO_FUZZ_FUZZSMT_REF:-z3}
fi
FUZZSMT_SEED_BASE=${PRIMO_FUZZ_FUZZSMT_SEED_BASE:-$(( ${SLURM_ARRAY_JOB_ID:-500000} % 900000 + 100000 ))}

# FuzzSMT parameter profiles: one ';'-separated entry per profile, assigned to
# shards round-robin (shard i gets profile i % count). Setting
# PRIMO_FUZZ_FUZZSMT_ARGS forces a single profile, which is what every
# pre-2026-08-01 docs/run-log.json command line does -- those stay reproducible.
#
# Why a ladder rather than one setting. Until 2026-08-01 every QF_LRA gate ran
# `-ms 1 -Ms 3 -mv 1 -Mv 3 -mf 1 -mp 1 -ma 1 -Ma 2`, and for QF_LRA that is the
# *stock default*: `-ms/-Ms` are sort counts and `-mf/-mp/-ma/-Ma` are function,
# predicate and arity knobs, all of which FuzzSMT applies only to the QF_UF*
# logics and silently ignores here (it does not reject them, so nothing ever
# complained). What was left -- `-Mv 3`, `-Mc 3`, `-Mbw 4` by default -- means
# every generated case in this project's history had at most three Real
# variables and **every coefficient in [1,15]**. Wide rationals, and hence the
# GMP bignum paths in DeltaRational and in Farkas coefficient combination, were
# never generated. Measured 2026-08-01, 100-150 cases per profile:
#
#   -Mbw 4 -> 16 (`wide`)   max literal 15 -> 54430, cost unchanged
#   -Mv 5 -Mc 4 (`tableau`) ~9x rows, 18s/100 cases, 1% timeouts
#   -bool-and / -bool-or    100% unsat / 100% sat, ~4s/100 cases
#
# The knobs that do *not* pay: `-ref 2` alone is a 7x file-size blowup and
# `-ref 3` a 20x one (generation itself starts hitting the wall), `-Mv 15`
# is 9x, and the two compose -- `-Mv 15 -ref 2` yields 2MB *per case*.
# `-bool-cnf` is unusable: FuzzSMT 0.3 emits a malformed let-block for it and
# z3 rejects the output too ("invalid let declaration"), so it is a generator
# bug, not something to work around here.
#
# `-bool-and` earns its shard: the random Boolean layer yields only ~11% unsat,
# and unsat is the discriminating outcome, since it is the one that exercises
# conflict detection and the Farkas certificate rather than just model search.
FUZZSMT_PROFILES_QF_LRA=$(cat <<'EOF'
-mv 1 -Mv 3 -mc 1 -Mc 3 -Mbw 4;-mv 1 -Mv 3 -mc 1 -Mc 3 -Mbw 16;-mv 2 -Mv 5 -mc 1 -Mc 4 -Mbw 16;-mv 2 -Mv 5 -mc 1 -Mc 4 -Mbw 16 -bool-and;-mv 2 -Mv 5 -mc 1 -Mc 4 -Mbw 16 -bool-or;-mv 1 -Mv 3 -mc 1 -Mc 3 -Mbw 32
EOF
)
FUZZSMT_PROFILES_DEFAULT="-ms 1 -Ms 2 -mv 1 -Mv 2 -mf 1 -mp 1 -ma 1 -Ma 2"

if [[ -n ${PRIMO_FUZZ_FUZZSMT_ARGS:-} ]]; then
  FUZZSMT_PROFILES=$PRIMO_FUZZ_FUZZSMT_ARGS
elif [[ -n ${PRIMO_FUZZ_FUZZSMT_PROFILES:-} ]]; then
  FUZZSMT_PROFILES=$PRIMO_FUZZ_FUZZSMT_PROFILES
elif [[ $FUZZSMT_LOGIC == QF_LRA ]]; then
  FUZZSMT_PROFILES=$FUZZSMT_PROFILES_QF_LRA
else
  FUZZSMT_PROFILES=$FUZZSMT_PROFILES_DEFAULT
fi

IFS=';' read -r -a fuzzsmt_profiles <<< "$FUZZSMT_PROFILES"

# The mutation seed corpus must follow the logic, like the mutation config,
# reference and keep-logic flags below already do. Until 2026-08-07 these two
# defaults were unconditional, so a gate submitted with
# PRIMO_FUZZ_FUZZSMT_LOGIC=QF_LRA mutated the QF_UF seeds under the QF_LRA
# operator-mutation config and the yices reference -- it ran clean and tested
# the wrong corpus, which is the exact failure family this file's preamble is
# about.
#
# QF_LRA defaults to the family-complete qf_lra_division corpus (34 seeds, 2
# per division family) rather than qf_lra_real (14 seeds covering 3 of 17
# families, plus gasburner which is not in the division at all). See
# docs/fuzz-coverage-qf-lra.md.
if [[ $FUZZSMT_LOGIC == QF_LRA ]]; then
  DEFAULT_SEEDS="$WORKDIR/benchmarks/seeds/qf_lra_division"
else
  DEFAULT_SEEDS="$WORKDIR/benchmarks/seeds/qf_uf"
fi

OPFUZZ_TASKS=${PRIMO_FUZZ_OPFUZZ_TASKS:-1}
OPFUZZ_SEEDS=${PRIMO_FUZZ_OPFUZZ_SEEDS:-"$DEFAULT_SEEDS"}
OPFUZZ_ITERATIONS=${PRIMO_FUZZ_OPFUZZ_ITERATIONS:-200}
OPFUZZ_TIMEOUT=${PRIMO_FUZZ_OPFUZZ_TIMEOUT:-5}

YINYANG_SHARDS=${PRIMO_FUZZ_YINYANG_SHARDS:-2}
YINYANG_SEEDS=${PRIMO_FUZZ_YINYANG_SEEDS:-"$DEFAULT_SEEDS"}
YINYANG_ITERATIONS=${PRIMO_FUZZ_YINYANG_ITERATIONS:-100}
YINYANG_TIMEOUT=${PRIMO_FUZZ_YINYANG_TIMEOUT:-5}
YINYANG_WALL_SECONDS=${PRIMO_FUZZ_YINYANG_WALL_SECONDS:-1500}

# typefuzz takes ';'-separated targets, so this adds yices as a third one
# alongside Primo and z3. It matters most exactly where the seed corpus was
# widened to: at a 20s reference timeout, z3 gives no oracle for either
# latendresse seed and yices answers both.
#
# Via the wrapper, not the binary: typefuzz strips (set-logic ...) from every
# mutant and yices needs it before Real is a sort, so bare yices-smt2 fails
# every mutant at line 1. See scripts/yices_smt2_ref.sh.
if [[ $FUZZSMT_LOGIC == QF_LRA ]]; then
  MUTATION_REF=${PRIMO_FUZZ_MUTATION_REF:-"z3 model_validate=true;$WORKDIR/scripts/yices_smt2_ref.sh"}
else
  MUTATION_REF=${PRIMO_FUZZ_MUTATION_REF:-"z3 model_validate=true"}
fi

# typefuzz's own default here is 20000 bytes, and it drops oversized seeds
# without saying so beyond a "N valid, M invalid" count. That silently
# truncated the family-complete QF_LRA corpus back to 11 of its 17 families
# (job 118543). 200000 matches the --max-bytes that
# scripts/build_fuzz_seed_corpus.py selects seeds under, so the limit no
# longer decides the corpus; the corpus builder does.
MUTATION_FILE_SIZE_LIMIT=${PRIMO_FUZZ_MUTATION_FILE_SIZE_LIMIT:-200000}

# Restrict mutation to operators that stay inside QF_LRA. YinYang's stock
# configs cover all of SMT-LIB and rewrite within sort signatures only, so on
# a QF_LRA seed they happily produce `(* x y)` from `(+ x y)` (config line
# `+,-,* :arity 2+`) and `(abs x)` from `(- x)` (line `abs,- :arity 1`).
# Those mutants are no longer QF_LRA and Primo rightly refuses them, which
# burns the mutation budget: measured 2026-08-02 over six seed families,
# opfuzz went from 47 of 72 mutants answerable to 72 of 72, with the same 72
# distinct mutants either way -- nothing is lost but the out-of-logic ones.
#
# This needs no change to the YinYang fork: both entry points take -c/--config.
if [[ $FUZZSMT_LOGIC == QF_LRA ]]; then
  OPFUZZ_CONFIG=${PRIMO_FUZZ_OPFUZZ_CONFIG:-"$WORKDIR/benchmarks/fuzz-configs/qf_lra_operator_mutations.txt"}
  YINYANG_CONFIG=${PRIMO_FUZZ_YINYANG_CONFIG:-"$WORKDIR/benchmarks/fuzz-configs/qf_lra_typefuzz.txt"}
  # Both come from contrib/patches/yinyang-mutant-validity.patch, applied by
  # contrib/install_yinyang.py. --keep-logic makes every solver judge the
  # mutant against the seed's logic (and is what lets yices-smt2 read a mutant
  # at all); --hoist-declarations repairs typefuzz emitting a declaration
  # after its first use. opfuzz does not need the latter -- it substitutes
  # operators in place and leaves command order alone.
  MUTATION_KEEP_LOGIC=${PRIMO_FUZZ_MUTATION_KEEP_LOGIC-1}
  MUTATION_HOIST_DECLARATIONS=${PRIMO_FUZZ_MUTATION_HOIST_DECLARATIONS-1}
else
  OPFUZZ_CONFIG=${PRIMO_FUZZ_OPFUZZ_CONFIG:-}
  YINYANG_CONFIG=${PRIMO_FUZZ_YINYANG_CONFIG:-}
  MUTATION_KEEP_LOGIC=${PRIMO_FUZZ_MUTATION_KEEP_LOGIC-}
  MUTATION_HOIST_DECLARATIONS=${PRIMO_FUZZ_MUTATION_HOIST_DECLARATIONS-}
fi

cd "$WORKDIR"
mkdir -p logs "$OUT_ROOT"

# The provisioned tooling (fuzzsmt, z3, cvc5) lives here. Put it on PATH
# inside the job rather than relying on --export=ALL to carry it in: that
# inherits the *submitting* shell's PATH, so a gate submitted over
# `ssh host bash -s` silently found no fuzzer while the same job submitted
# from an interactive shell ran fine. Job 117585 reported COMPLETED with
# SKIPPED_MISSING_FUZZER as its only summary row -- a green gate that tested
# nothing.
PATH="$WORKDIR/contrib/local/bin:$PATH"
# YinYang's opfuzz/typefuzz are installed into a workspace-local venv, not
# contrib/local/bin. Without this the mutation tasks fail with "'opfuzz' not
# found" while the FuzzSMT shards pass, so the gate reports six clean shards
# and three failures rather than a missing capability -- observed on job
# 120180. The QF_LRA gate has always done this; this one had not.
if [ -d "$WORKDIR/.venv-yinyang/bin" ]; then
  PATH="$WORKDIR/.venv-yinyang/bin:$PATH"
fi
export PATH

# A sanitizer build made by scripts/build_slurm_fuzz.sh links against the
# EasyBuild GCCcore runtime, which compute nodes do not have on their default
# search path -- the binary dies with "libasan.so.6: cannot open shared object
# file" before solving anything. Harmless for a Release build, required for an
# ASAN one, so it is set unconditionally.
GCCCORE_LIB=${PRIMO_GCCCORE_LIB:-/export/eb/software/GCCcore/11.3.0/lib64}
if [[ -d $GCCCORE_LIB ]]; then
  LD_LIBRARY_PATH="$GCCCORE_LIB:${LD_LIBRARY_PATH:-}"
  export LD_LIBRARY_PATH
fi

# And fail loudly rather than skipping: fuzz_external.py treats a missing
# fuzzer as "skip this stage" and exits 0, which is the right behaviour for a
# local smoke run and exactly the wrong one for a gate whose entire purpose
# is to have run.
# Every reference this gate is configured to use has to be present, not just
# z3: a missing second reference silently narrows the gate rather than failing
# it, which is the whole failure family this check exists for.
required_tools=(fuzzsmt z3)
for ref in $FUZZSMT_REF ${MUTATION_REF//;/ }; do
  case $ref in
    *=*) continue ;;  # solver options such as model_validate=true
  esac
  required_tools+=("$ref")
done
for required in "${required_tools[@]}"; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "required tool '$required' not found on PATH; refusing to report a gate that ran nothing" >&2
    exit 2
  fi
done

opfuzz_start=$FUZZSMT_SHARDS
yinyang_start=$((opfuzz_start + OPFUZZ_TASKS))
total_tasks=$((yinyang_start + YINYANG_SHARDS))

if (( TASK_ID >= total_tasks )); then
  echo "task $TASK_ID is outside the active $total_tasks-task range; exiting"
  exit 0
fi

# PRIMO_FUZZ_SOLVER may carry flags (e.g. "path/primo --lra-fixed-elimination"),
# since every --solver use below interpolates it into a string that
# fuzz_external.py shlex-splits. The provenance banner has to take just the
# binary, or it degrades into a "command not found" that `|| true` hides --
# and that banner is the only record of which build a gate actually ran.
IFS=' ' read -r -a solver_words <<< "$SOLVER_BIN"
"${solver_words[0]}" --version || true

if (( TASK_ID < opfuzz_start )); then
  shard=$TASK_ID
  seed=$((FUZZSMT_SEED_BASE + shard))
  profile_index=$((shard % ${#fuzzsmt_profiles[@]}))
  profile=${fuzzsmt_profiles[$profile_index]}
  out="$OUT_ROOT/fuzzsmt/shard-$shard"
  mkdir -p "$out"
  # Record the profile: with a ladder, "shard 3 was clean" only means something
  # if the log says which parameters shard 3 ran.
  echo "task $TASK_ID: fuzzsmt shard $shard seed $seed profile $profile_index [$profile]"
  python3 scripts/fuzz_external.py \
    --fuzzer fuzzsmt \
    --solver "$SOLVER_BIN --quiet" \
    --refs $FUZZSMT_REF \
    --logic "$FUZZSMT_LOGIC" \
    --count "$FUZZSMT_COUNT" \
    --timeout "$FUZZSMT_TIMEOUT" \
    --seed "$seed" \
    --fuzzer-args "$profile" \
    --out "$out"
elif (( TASK_ID < yinyang_start )); then
  out="$OUT_ROOT/opfuzz/task-$TASK_ID"
  mkdir -p "$out"
  echo "task $TASK_ID: opfuzz over $OPFUZZ_SEEDS"
  scripts/yinyang_fuzz.sh \
    --typefuzz opfuzz \
    ${OPFUZZ_CONFIG:+--config "$OPFUZZ_CONFIG"} \
    ${MUTATION_KEEP_LOGIC:+--keep-logic} \
    --seeds "$OPFUZZ_SEEDS" \
    --solver "$SOLVER_BIN --quiet" \
    --ref "$MUTATION_REF" \
    --seconds "$YINYANG_WALL_SECONDS" \
    --iterations "$OPFUZZ_ITERATIONS" \
    --solver-timeout "$OPFUZZ_TIMEOUT" \
    --file-size-limit "$MUTATION_FILE_SIZE_LIMIT" \
    --bugs "$out/bugs" \
    --scratch "$out/scratch"
else
  out="$OUT_ROOT/yinyang/task-$TASK_ID"
  mkdir -p "$out"
  echo "task $TASK_ID: yinyang typefuzz over $YINYANG_SEEDS"
  scripts/yinyang_fuzz.sh \
    ${YINYANG_CONFIG:+--config "$YINYANG_CONFIG"} \
    ${MUTATION_KEEP_LOGIC:+--keep-logic} \
    ${MUTATION_HOIST_DECLARATIONS:+--hoist-declarations} \
    --seeds "$YINYANG_SEEDS" \
    --solver "$SOLVER_BIN --quiet" \
    --ref "$MUTATION_REF" \
    --seconds "$YINYANG_WALL_SECONDS" \
    --iterations "$YINYANG_ITERATIONS" \
    --solver-timeout "$YINYANG_TIMEOUT" \
    --file-size-limit "$MUTATION_FILE_SIZE_LIMIT" \
    --bugs "$out/bugs" \
    --scratch "$out/scratch"
fi
