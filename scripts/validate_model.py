#!/usr/bin/env python3
"""Validate produced models where possible.

This compatibility entry point currently delegates to the QF_UF Dolmen model
validator. Add a dispatcher here when Primo supports model validation for
multiple logics.
"""

from __future__ import annotations

from validate_qfuf_models_with_dolmen import main as validate_qfuf_models


if __name__ == "__main__":
    raise SystemExit(validate_qfuf_models())
