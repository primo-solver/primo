#!/usr/bin/env bash
# Run YinYang/typefuzz against Primo and a reference SMT solver.

set -euo pipefail

SEEDS="benchmarks/seeds/qf_uf"
SOLVER="build/primo --quiet"
REF="z3 model_validate=true"
WALL_SECONDS=300
ITERATIONS=50
SOLVER_TIMEOUT=8
BUGS="yinyang_bugs"
SCRATCH="yinyang_scratch"
FILE_SIZE_LIMIT=20000
CONFIG=""
KEEP_LOGIC=0
HOIST_DECLARATIONS=0
TYPEFUZZ="${TYPEFUZZ:-typefuzz}"
YINYANG_INSTALL_SPEC="${YINYANG_INSTALL_SPEC:-yinyang}"
DIAGNOSE=0
KEEP_MUTANTS=0
EXTRA_ARGS=()

usage() {
    cat <<'EOF'
Usage: scripts/yinyang_fuzz.sh [options] [-- extra typefuzz args...]

Options:
  --seeds PATH             SMT-LIB seed file or directory
  --solver CMD             Primo command
  --ref CMD                reference solver command
  --seconds N              wall-clock limit for typefuzz
  --iterations N           YinYang mutations per seed
  --solver-timeout N       per-solver timeout in seconds
  --bugs DIR               bug output directory
  --scratch DIR            scratch directory
  --file-size-limit N      max seed size in bytes
  --config PATH            operator/signature config restricting mutations
  --keep-logic             keep the seed's (set-logic ...) in mutants
  --hoist-declarations     emit declarations before their first use
  --typefuzz PATH          typefuzz executable
  --diagnose               forward solver output to stdout
  --keep-mutants           keep generated mutants in scratch
  -h, --help               show this help

Install:
  python3 -m pip install "$YINYANG_INSTALL_SPEC" z3-solver

For QF_LRA/QF_UFLRA campaigns, set YINYANG_INSTALL_SPEC to the patched fork
until the package release includes the linear-Real mutation fix.
  python3 -m pip install 'git+https://github.com/MikolasJanota/yinyang.git' z3-solver

Example:
  scripts/yinyang_fuzz.sh \
    --seeds benchmarks/seeds/qf_uf \
    --solver 'build/primo --quiet' \
    --ref 'z3 model_validate=true' \
    --seconds 600 --iterations 100
EOF
}

while (($#)); do
    case "$1" in
        --seeds) SEEDS="$2"; shift 2 ;;
        --solver) SOLVER="$2"; shift 2 ;;
        --ref) REF="$2"; shift 2 ;;
        --seconds) WALL_SECONDS="$2"; shift 2 ;;
        --iterations) ITERATIONS="$2"; shift 2 ;;
        --solver-timeout) SOLVER_TIMEOUT="$2"; shift 2 ;;
        --bugs) BUGS="$2"; shift 2 ;;
        --scratch) SCRATCH="$2"; shift 2 ;;
        --file-size-limit) FILE_SIZE_LIMIT="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --keep-logic) KEEP_LOGIC=1; shift ;;
        --hoist-declarations) HOIST_DECLARATIONS=1; shift ;;
        --typefuzz) TYPEFUZZ="$2"; shift 2 ;;
        --diagnose) DIAGNOSE=1; shift ;;
        --keep-mutants) KEEP_MUTANTS=1; shift ;;
        -h|--help) usage; exit 0 ;;
        --) shift; EXTRA_ARGS=("$@"); break ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if ! command -v "$TYPEFUZZ" >/dev/null 2>&1; then
    cat >&2 <<EOF
Error: '$TYPEFUZZ' not found.

Install YinYang first:
  python3 -m pip install "$YINYANG_INSTALL_SPEC" z3-solver

Or pass --typefuzz /path/to/typefuzz.
EOF
    exit 127
fi

if [[ ! -e "$SEEDS" ]]; then
    echo "Error: seed path does not exist: $SEEDS" >&2
    exit 1
fi

mkdir -p "$BUGS" "$SCRATCH"

# typefuzz's -L silently drops any seed larger than the limit; it reports the
# loss only as "N valid, M invalid" in its summary, which reads like a
# well-sortedness result rather than "your corpus was truncated". On
# 2026-08-01 that discarded 12 of the 34 seeds in the family-complete QF_LRA
# corpus -- every seed from latendresse, LassoRanker, Heizmann, miplib, TM and
# tropical-matrix, i.e. exactly the families the corpus had just been widened
# to include. Say so up front, so the campaign log records what it did not fuzz.
if [[ -d "$SEEDS" ]]; then
    oversized=$(find "$SEEDS" -type f -name '*.smt2' -size +"${FILE_SIZE_LIMIT}"c | wc -l)
    total=$(find "$SEEDS" -type f -name '*.smt2' | wc -l)
    if (( oversized > 0 )); then
        echo "warning: $oversized of $total seeds exceed --file-size-limit ${FILE_SIZE_LIMIT}B and will be skipped:" >&2
        find "$SEEDS" -type f -name '*.smt2' -size +"${FILE_SIZE_LIMIT}"c -printf '  %f (%s B)\n' >&2
    fi
fi

cmd=(
    "$TYPEFUZZ"
    -i "$ITERATIONS"
    -t "$SOLVER_TIMEOUT"
    -b "$BUGS"
    -s "$SCRATCH"
    -L "$FILE_SIZE_LIMIT"
)

if ((DIAGNOSE)); then
    cmd+=(-d)
fi
if [[ -n $CONFIG ]]; then
    if [[ ! -f $CONFIG ]]; then
        echo "Error: --config file does not exist: $CONFIG" >&2
        exit 1
    fi
    cmd+=(-c "$CONFIG")
fi
if ((KEEP_LOGIC)); then
    cmd+=(-kl)
fi
if ((HOIST_DECLARATIONS)); then
    cmd+=(-hd)
fi
if ((KEEP_MUTANTS)); then
    cmd+=(-k)
fi
cmd+=("${EXTRA_ARGS[@]}")
cmd+=("$SOLVER;$REF" "$SEEDS")

echo "YinYang command:"
printf '  %q' timeout "$WALL_SECONDS" "${cmd[@]}"
echo

exit_code=0
timeout "$WALL_SECONDS" "${cmd[@]}" || exit_code=$?

# Reaching --seconds is how a time-boxed campaign is *supposed* to end, but
# `timeout` reports it as exit 124, which under the gate's `set -e` marks the
# whole Slurm task FAILED. Job 118548's four typefuzz shards each ran the full
# 90 minutes, found nothing, and were recorded as failures. Distinguish the
# two, and say what the run is and is not evidence for: typefuzz writes each
# bug to --bugs as it finds it, so an empty bugs/ after a wall-terminated run
# does mean no bug was found in that time -- but the seeds-processed summary
# never printed, so it is not evidence that every seed was covered.
found=0
if [[ -d $BUGS ]]; then
    found=$(find "$BUGS" -mindepth 1 -type f | wc -l)
fi

if (( exit_code == 124 )); then
    echo "yinyang_fuzz: reached the ${WALL_SECONDS}s wall; this is a time-boxed run, not a failure."
    echo "yinyang_fuzz: $found bug file(s) in $BUGS. No seeds-processed summary: the run was cut short, so coverage of every seed is NOT established."
    exit 0
fi

if (( exit_code != 0 )); then
    echo "yinyang_fuzz: $TYPEFUZZ exited $exit_code ($found bug file(s) in $BUGS)" >&2
    exit "$exit_code"
fi

echo "yinyang_fuzz: completed; $found bug file(s) in $BUGS"
