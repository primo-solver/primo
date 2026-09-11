import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from incremental_uf_gate import generated, scoped
from native_uf_sessions import classify, query_metadata
from validate_incremental import QueryJournal, command_spans


class UfGateTests(unittest.TestCase):
    def test_only_fresh_command_status_is_an_oracle(self):
        metadata = query_metadata('''(set-logic QF_UF)
(set-info :source "(set-info :status unsat)")
(set-info :status sat) (declare-const p Bool) (check-sat)
(push 2) (assert p) (check-sat) (pop 2)
(set-info :status unsat) (reset-assertions) (check-sat)''')
        self.assertEqual(metadata, {"answers": ["sat", "unknown", "unknown"],
                                    "queries": 3, "push": 2, "pop": 2})

    def test_generated_scopes_and_reproducibility(self):
        for seed in range(30):
            text = generated(seed)
            self.assertEqual(text, generated(seed))
            journal = QueryJournal()
            queries = 0
            for start, end, head in command_spans(text):
                journal.apply(text[start:end], head)
                queries += head == ("check-sat",)
            self.assertEqual(queries, 9)
            self.assertEqual(len(journal.frames), 1)

    def test_mutation_does_not_reuse_popped_assertions(self):
        text = """(set-logic QF_UF)
(push 1) (declare-const hidden Bool) (assert hidden) (pop 1) (check-sat)"""
        output = scoped([(text[a:b], h) for a, b, h in command_spans(text)], 0, True)
        self.assertNotIn("hidden", output.split("(pop 1)", 1)[1])

    def test_timeout_and_exit_override_partial_answers(self):
        self.assertEqual(classify("sat\n", 0, True, ["sat"])["outcome"], "timeout")
        self.assertEqual(classify("sat\n", 1, False, ["sat"])["outcome"], "error")
        self.assertEqual(classify("sat\n", 0, False, ["unknown"])["outcome"], "complete")
        self.assertEqual(classify("unsat\n", 0, True, ["sat"])["outcome"], "wrong-answer")
        self.assertEqual(classify("sat\nsat\n", 0, False, ["sat"])["outcome"], "error")


if __name__ == "__main__":
    unittest.main()
