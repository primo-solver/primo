import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from check_incremental_corpus import scoped_session
from validate_incremental import QueryJournal, command_spans


class CorpusSessionTests(unittest.TestCase):
    def test_scopes_redeclare_all_original_symbols(self):
        source = (
            '(set-info :source "text (;)")(set-info :status sat)(set-logic QF_UF)'
            "(declare-sort U 0)(declare-const |a| U)(declare-fun f (U) U)"
            "(assert (= (f a) a))(check-sat)(exit)"
        )
        text, expected = scoped_session(source, "sat", "QF_UF")
        self.assertEqual(expected, ["sat", "sat", "unsat", "sat", "sat", "sat", "sat"])
        journal, snapshots = QueryJournal(), []
        for start, end, head in command_spans(text):
            journal.apply(text[start:end], head)
            if head == ("check-sat",):
                snapshots.append(journal.snapshot())
        self.assertEqual(len(snapshots), 7)
        self.assertEqual(snapshots[0], snapshots[6])
        self.assertEqual(snapshots[5], "(set-logic QF_UF)\n(check-sat)")
        self.assertIn("(assert (= (f a) a))", snapshots[0])
        self.assertIn("(assert (not $primo_incremental_probe))", snapshots[2])
        self.assertNotIn("(assert (not $primo_incremental_probe))", snapshots[3])

    def test_reject_mismatched_status_or_incremental_source(self):
        prefix = "(set-info :status sat)(set-logic QF_UF)"
        for text, expected in [
            (prefix + "(check-sat)", "unsat"),
            (prefix + "(push 1)(check-sat)", "sat"),
            (prefix + "(check-sat)(check-sat)", "sat"),
        ]:
            with self.assertRaises(ValueError):
                scoped_session(text, expected, "QF_UF")


if __name__ == "__main__":
    unittest.main()
