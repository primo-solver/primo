#!/usr/bin/env bash
# Build the current manual or the frozen historical book.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENV="${PRIMO_DOCS_VENV:-$REPO_ROOT/.venv-docs}"
BOOK_DIR="${PRIMO_DOCS_DIR:-docs}"

# Keep the two book outputs separate while sharing the existing toolchain.
case "$BOOK_DIR" in
    docs|docs-history) ;;
    *) echo "error: PRIMO_DOCS_DIR must be docs or docs-history" >&2; exit 2 ;;
esac

cd "$REPO_ROOT"

if [[ ! -x "$VENV/bin/python" ]]; then
    python3 -m venv "$VENV"
    "$VENV/bin/python" -m pip --disable-pip-version-check install --upgrade pip
fi

"$VENV/bin/python" -m pip --disable-pip-version-check install -r docs/requirements.txt

# Local previews are incremental; validation passes --all explicitly.
BUILD_ARGS=()
# Warnings remain errors in either mode. PRIMO_DOCS_STRICT=0 permits a preview
# despite warnings; use a strict full build before accepting documentation.
if [[ "${PRIMO_DOCS_STRICT:-1}" != "0" ]]; then
    BUILD_ARGS+=(-W --keep-going)
fi

"$VENV/bin/python" -c '
import sys
from jupyter_book.cli.main import main
main(args=["build", sys.argv[1]] + sys.argv[2:], standalone_mode=True)
' "$BOOK_DIR" "${BUILD_ARGS[@]}" "$@"

"$VENV/bin/python" scripts/collect_doc_notices.py \
    --output "$BOOK_DIR/_build/html/_static/licenses"
