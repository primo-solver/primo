#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import random
import re
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

    def test_model_reconstruction_cases_keep_the_completion_witness(self) -> None:
        # Answer comparison alone cannot reject a bad model. Keep the exact
        # preprocessing/table interaction in every draw so the Dolmen phase of
        # the official gate deterministically exercises it.
        rng = random.Random(31)
        for index in range(100):
            case = MODULE.generate_case(rng, index, "model-reconstruction")
            self.assertEqual(case.count("(assert "), 2)
            self.assertRegex(case, r"\(= (?:\(tb [pqr]\) [ab]|[ab] \(tb [pqr]\))\)")
            self.assertTrue(
                "(= (tb true) (tb false))" in case
                or "(not (distinct (tb true) (tb false)))" in case
            )
            self.assertNotRegex(case, r"\(assert \(?not ?[pqr]\)?\)")

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



class RandomShapeTests(unittest.TestCase):
    """The grammar-based shape, which exists to reach shapes nobody wrote down.

    A template generator fails loudly when it is wrong. A random one fails on
    one draw in a few hundred, halfway through a campaign -- which is exactly
    what happened on its first run (`empty range in randrange(2, 2)` when a
    single formula survived the skeleton fold), so these tests hammer the
    generator itself rather than the solver.
    """

    def test_thousands_of_draws_never_raise(self) -> None:
        rng = random.Random(99)
        for index in range(3000):
            case = MODULE.generate_case(rng, index, "random")
            self.assertIn("(check-sat)", case)
            self.assertIn("(set-logic QF_UF)", case)

    def test_generated_cases_are_balanced_and_declared(self) -> None:
        rng = random.Random(7)
        for index in range(500):
            case = MODULE.generate_case(rng, index, "random")
            self.assertEqual(case.count("("), case.count(")"),
                             msg=f"unbalanced parentheses in case {index}")
            used = set(re.findall(r"\(fn(\d+) ", case))
            declared = set(re.findall(r"\(declare-fun fn(\d+) ", case))
            self.assertTrue(used <= declared,
                            msg=f"case {index} applies an undeclared function")

    def test_compound_terms_reach_argument_position(self) -> None:
        # A compound Bool term as a function ARGUMENT is not passed through:
        # the frontend binds it to a fresh Bool constant with a defining
        # equivalence, and that auxiliary is then a UF argument whose polarity
        # comes from the equivalence. That is the same interaction that
        # produced the pure-theory-filter bug, so the coverage matters.
        rng = random.Random(17)
        in_argument = 0
        with_ite = 0
        for index in range(600):
            case = MODULE.generate_case(rng, index, "random")
            if re.search(r"\(fn\d+[^()]*\((and|or|not|=>|xor|ite|=|distinct) ", case):
                in_argument += 1
            if "(ite " in case:
                with_ite += 1
        self.assertGreater(in_argument, 30)
        self.assertGreater(with_ite, 100)

    def test_random_shape_puts_bool_constants_in_the_skeleton(self) -> None:
        # The property the hand-written shapes all lacked: a Bool constant
        # appearing as a propositional literal, not only as an argument.
        rng = random.Random(5)
        skeleton_hits = 0
        for index in range(400):
            case = MODULE.generate_case(rng, index, "random")
            for line in case.splitlines():
                m = re.match(r"^\(assert (b\d+)\)$", line)
                if m:
                    skeleton_hits += 1
                    break
        self.assertGreater(skeleton_hits, 20)


if __name__ == "__main__":
    unittest.main()
