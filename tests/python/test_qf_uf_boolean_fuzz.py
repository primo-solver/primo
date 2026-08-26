#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import random
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts" / "qf_uf_boolean_fuzz.py"
SPEC = importlib.util.spec_from_file_location("qf_uf_boolean_fuzz", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class GeneratedCaseTests(unittest.TestCase):
    """The two properties that make this harness able to catch what the old one could not."""

    def test_every_case_is_pure_qf_uf(self) -> None:
        # The QF_UFLRA harness could not reach the pure QF_UF solving path at
        # all, because every case it emits carries a Real constraint. If any
        # arithmetic leaks in here, this generator loses the same way.
        rng = random.Random(11)
        for index in range(300):
            case = MODULE.generate_case(rng, index, "all")
            self.assertIn("(set-logic QF_UF)", case)
            for token in ("Real", "Int", "(+ ", "(- ", "(* ", "(< ", "(<= ", "(> ", "(>= "):
                self.assertNotIn(token, case, msg=f"arithmetic leaked into case {index}")

    def test_converse_cases_keep_every_assertion(self) -> None:
        # The converse shape is unsat only as a whole: injectivity of f, the
        # equality of images, and the consumer of the derived argument
        # equality. Sampling a subset the way the other shapes do would emit
        # satisfiable formulas that prove nothing, so converse_case is used
        # verbatim -- this pins that.
        rng = random.Random(3)
        for index in range(100):
            case = MODULE.generate_case(rng, index, "converse")
            self.assertIn("(distinct (f true) (f false))", case)
            self.assertIn("(= (f ", case)
            self.assertGreaterEqual(case.count("(assert "), 3)

    def test_every_shape_generates(self) -> None:
        rng = random.Random(5)
        for shape in MODULE.SHAPES:
            case = MODULE.generate_case(rng, 0, shape)
            self.assertIn("(check-sat)", case)
            self.assertIn("(assert ", case)


class ClassificationTests(unittest.TestCase):
    def test_a_wrong_answer_is_not_reported_as_a_pass(self) -> None:
        self.assertEqual(MODULE.classify("sat", "unsat"), "OUR_SAT_REF_UNSAT")
        self.assertEqual(MODULE.classify("unsat", "sat"), "OUR_UNSAT_REF_SAT")

    def test_our_own_failures_outrank_the_reference_state(self) -> None:
        self.assertEqual(MODULE.classify("CRASH", "unsat"), "CRASH")
        self.assertEqual(MODULE.classify("UNSUPPORTED_INPUT", "unsat"), "UNSUPPORTED_INPUT")

    def test_an_unusable_reference_is_not_a_pass(self) -> None:
        self.assertEqual(MODULE.classify("sat", "TIMEOUT"), "REFERENCE_UNUSABLE")
        self.assertEqual(MODULE.classify("sat", "MISSING"), "REFERENCE_UNUSABLE")

    def test_agreement_is_a_pass(self) -> None:
        self.assertEqual(MODULE.classify("unsat", "unsat"), "PASS")


if __name__ == "__main__":
    unittest.main()
