#!/usr/bin/env python3
"""Validate produced models where possible.

This compatibility entry point delegates to the Dolmen-backed model validator.
The implementation file still carries its original QF_UF name, but the Dolmen
check also covers the currently printed QF_LRA, QF_LIA, QF_UFLRA, and QF_UFLIA
model subsets.
"""

from __future__ import annotations

from validate_qfuf_models_with_dolmen import main as validate_qfuf_models

if __name__ == "__main__":
    raise SystemExit(validate_qfuf_models())
