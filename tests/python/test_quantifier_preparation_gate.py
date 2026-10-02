"""The gate must reject post-answer failures and preserve binder scopes."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

from quantifier_preparation_gate import (
    classify,
    mutate,
    preprocessing_stopped_before_search,
    quantified,
)
from summarize_quantifier_preparation_gate import verify_case
from validate_qfuf_models_with_dolmen import command_spans


class QuantifierPreparationGateTests(unittest.TestCase):
    def test_preprocessing_diagnostic_rejects_search_and_missing_statistics(self):
        idle = ":sat.solve-calls 0\n:time.solve-us 0\n"
        for answer in ("unknown", "unsat"):
            self.assertTrue(preprocessing_stopped_before_search(answer, idle))
        for answer in ("sat", "error", "crash", "timeout", "unsupported"):
            self.assertFalse(preprocessing_stopped_before_search(answer, idle))
        self.assertFalse(preprocessing_stopped_before_search("unknown", ""))
        for key in (
            ":sat.solve-calls",
            ":quant.instances",
            ":quant.skolem-lemmas",
            ":time.solve-us",
        ):
            self.assertFalse(
                preprocessing_stopped_before_search("unknown", idle + f"{key} 1\n")
            )

    def test_summary_rejects_missing_duplicate_and_failed_answers(self):
        row = {"profile": "primo", "answer": "sat", "exit": 0, "input_sha256": "same"}
        reference = row | {"profile": "ref"}
        verify_case([row, reference], {"primo", "ref"})
        for rows in [
            [row],
            [row, row],
            [row | {"exit": 1}, reference],
            [row | {"answer": "unsat"}, reference],
            [row | {"input_sha256": "different"}, reference],
        ]:
            with self.assertRaises(ValueError):
                verify_case(rows, {"primo", "ref"})

    def test_answer_does_not_hide_a_crash(self):
        self.assertEqual(classify("done", -11, "sat\n"), "crash")
        self.assertEqual(classify("done", 2, "unsat\n"), "error")
        self.assertEqual(classify("timeout", None, "sat\n"), "timeout")
        self.assertEqual(classify("done", 0, 'sat\n(error "late")\n'), "error")
        self.assertEqual(classify("done", 0, "sat\nunsat\n"), "error")

    def test_mutations_keep_commands_and_avoid_capture(self):
        source = """(set-logic UF)
(declare-const primo_gate_12_0_0 Bool)
(assert primo_gate_12_0_0)
(declare-const p Bool)
(assert (not p))
(check-sat)
"""
        result = mutate(source, 12, 0)
        commands = [head[0] for _, _, head in command_spans(result)]
        self.assertEqual(
            commands,
            [
                "set-logic",
                "declare-const",
                "assert",
                "declare-const",
                "assert",
                "check-sat",
            ],
        )
        self.assertNotIn("((|primo_gate_12_0_0| Bool))", result)

    def test_generator_has_sat_and_unsat_oracles(self):
        outcomes = set()
        for index in range(32):
            text, expected = quantified(17, index)
            outcomes.add(expected)
            self.assertEqual(command_spans(text)[-1][2][0], "check-sat")
        self.assertEqual(outcomes, {"sat", "unsat"})
