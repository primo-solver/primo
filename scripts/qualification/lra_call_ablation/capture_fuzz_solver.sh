#!/usr/bin/env bash
# Each native invocation gets an independent, retained capture directory.
set -euo pipefail
capture_dir=$(mktemp -d "${PRIMO_CAPTURE_FUZZ_DIR:?}/case-XXXXXXXX")
exec "${PRIMO_WORKDIR:?}/build-slurm-rel/primo" \
 --lra-call-profile "$capture_dir/calls.jsonl" \
 --lra-call-capture "$capture_dir/input" "$@"
