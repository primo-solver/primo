import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

with patch.object(
    sys, "path", [str(Path(__file__).resolve().parents[2] / "scripts"), *sys.path]
):
    from prepare_lra_case_study import add_candidate, conjunction_id, select_records


def problem(status="sat", threshold=1):
    return (
        f"(set-logic QF_LRA)\n(set-info :status {status})\n"
        f"(declare-fun v0 () Real)\n(assert (<= v0 {threshold}))\n(check-sat)\n"
    )


def candidate(identity, parent, pivots=128, retained=False):
    return {
        "conjunction_id": identity,
        "retained": retained,
        "occurrences": [{"parent": parent, "call": {"pivots": pivots}}],
    }


class CaseStudySelectionTests(unittest.TestCase):
    def test_comments_status_and_spacing_do_not_create_samples(self):
        self.assertEqual(
            conjunction_id(problem()),
            conjunction_id(
                "; checker 2 call 20\n" + problem("unknown").replace(" ", "  ")
            ),
        )
        self.assertNotEqual(
            conjunction_id(problem()), conjunction_id(problem(threshold=2))
        )

    def test_unknown_label_merges_but_conflicting_known_label_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "input.smt2"
            pool = {}
            p.write_text(problem("unknown"))
            add_candidate(pool, p, "unknown", {"parent": "a/p"}, retained=True)
            p.write_text(problem("sat"))
            add_candidate(pool, p, "sat", {"parent": "b/q"})
            row = next(iter(pool.values()))
            self.assertEqual(row["expected"], "sat")
            self.assertTrue(row["retained"])
            self.assertEqual(len(row["occurrences"]), 2)
            p.write_text(problem("unsat"))
            with self.assertRaisesRegex(ValueError, "contradictory expected"):
                add_candidate(pool, p, "unsat", {"parent": "c/r"})

    def test_family_parent_balance_retention_caps_and_determinism(self):
        records = [candidate("original", "a/one", retained=True)]
        for family in ["a", "b"]:
            for parent in ["one", "two"]:
                records += [
                    candidate(f"{family}{parent}{i}", f"{family}/{parent}", i * 64)
                    for i in range(5)
                ]
        selected = select_records(records, 8, 42, 2, 4)
        self.assertIn(records[0], selected)
        self.assertEqual(len({r["conjunction_id"] for r in selected}), 8)
        self.assertEqual(selected, select_records(list(reversed(records)), 8, 42, 2, 4))
        counts = {}
        for r in selected:
            p = r["occurrences"][0]["parent"]
            counts[p] = counts.get(p, 0) + 1
        self.assertTrue(all(n == 2 for n in counts.values()))
        with self.assertRaisesRegex(ValueError, "only 8"):
            select_records(records, 9, 42, 2, 4)

    def test_retained_inputs_cannot_silently_violate_caps(self):
        records = [candidate(str(i), "a/p", retained=True) for i in range(2)]
        with self.assertRaisesRegex(ValueError, "retained.*quota"):
            select_records(records, 2, 42, 1, 4)


if __name__ == "__main__":
    unittest.main()
