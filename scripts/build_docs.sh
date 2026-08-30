#!/usr/bin/env bash
# Build the Jupyter Book developer documentation.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENV="${PRIMO_DOCS_VENV:-$REPO_ROOT/.venv-docs}"

cd "$REPO_ROOT"

if [[ ! -x "$VENV/bin/python" ]]; then
    python3 -m venv "$VENV"
    "$VENV/bin/python" -m pip install --upgrade pip
fi

"$VENV/bin/python" -m pip install -r docs/requirements.txt

# --all is not an optimization knob, it is what makes this check mean anything.
# Sphinx only emits warnings for the documents it re-reads, so an incremental
# build reports a clean tree whenever nothing changed, and reports nothing about
# a cross-reference that another file just broke. A full build of this site
# takes about 11 seconds, so there is no reason to ever run the unreliable one.
#
# -W turns those warnings into a failure, and --keep-going reports all of them
# rather than stopping at the first. Set PRIMO_DOCS_STRICT=0 to build anyway
# while iterating; do not commit against a build that needed it.
BUILD_ARGS=(--all)
if [[ "${PRIMO_DOCS_STRICT:-1}" != "0" ]]; then
    BUILD_ARGS+=(-W --keep-going)
fi

"$VENV/bin/python" -c '
import sys
from jupyter_book.cli.main import main
main(args=["build", "docs"] + sys.argv[1:], standalone_mode=True)
' "${BUILD_ARGS[@]}" "$@"
