#!/usr/bin/env bash
# yices-smt2 wrapper that supplies a (set-logic ...) when the input lacks one.
#
# Why this exists. yices-smt2 needs the logic set before `Real` is a defined
# sort, and it has no --logic flag; z3 is lenient and infers a default. That
# difference is invisible on benchmark files, which all carry (set-logic ...),
# and fatal on YinYang mutants, which do not: typefuzz strips the command, so
# all 200 mutants sampled from job 118611 lacked it and yices failed every one
# with
#
#     (error "at line 1, column 21: undefined sort: Real")
#
# at line 1, before reaching any content. As a reference solver it then
# contributed nothing while still costing a process launch per mutant --
# visible as YinYang's effective-call rate falling from 65.1% to 42.8% when
# yices was added as a third target.
#
# Prepending the logic is safe here because typefuzz's mutations are
# type-aware and preserve sorts, so a QF_LRA seed yields QF_LRA mutants. If a
# mutant really did leave the logic, yices would reject it and that rejection
# is the honest outcome -- this script does not suppress errors.
#
# Usage: scripts/yices_smt2_ref.sh [yices options...] FILE
# The logic comes from PRIMO_YICES_LOGIC (default QF_LRA).

set -uo pipefail

logic=${PRIMO_YICES_LOGIC:-QF_LRA}
yices=${PRIMO_YICES_BIN:-yices-smt2}

if (($# == 0)); then
    echo "usage: $0 [yices options...] FILE" >&2
    exit 2
fi

file=${!#}

if [[ ! -f $file ]] || grep -q '(set-logic' "$file"; then
    exec "$yices" "$@"
fi

patched=$(mktemp -t yices-ref-XXXXXX.smt2)
trap 'rm -f "$patched"' EXIT
{
    echo "(set-logic $logic)"
    cat "$file"
} >"$patched"

"$yices" "${@:1:$#-1}" "$patched"
