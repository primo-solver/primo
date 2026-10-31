import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

with patch.object(
    sys, "path", [str(Path(__file__).resolve().parents[2] / "scripts"), *sys.path]
):
    from run_lra_simplex_ablation import check_run, decisive, model_groups, schedule


class SimplexAblationTests(unittest.TestCase):
    def setUp(self):
        self.plan = json.loads(
            (
                Path(__file__).resolve().parents[2]
                / "benchmarks/qf-lra-simplex-ablation/plan.json"
            ).read_text()
        )

    def test_matrix_covers_each_factor_combination_once(self):
        factors = {
            (c["pivot_rule"], c["gaussian_elimination"], c["fixed_elimination"])
            for c in self.plan["configurations"]
        }
        self.assertEqual(len(factors), 16)
        self.assertEqual(len(self.plan["configurations"]), 16)
        baseline = next(
            c for c in self.plan["configurations"] if c["id"] == self.plan["baseline"]
        )
        self.assertEqual(
            (
                baseline["pivot_rule"],
                baseline["gaussian_elimination"],
                baseline["fixed_elimination"],
            ),
            ("sparse", True, False),
        )

    def test_schedule_pairs_inputs_without_losing_repeats_or_configs(self):
        names = [f"cubes/{index}.smt2" for index in range(13)]
        parts = [list(schedule(self.plan, names, shard, 4)) for shard in range(4)]
        rows = [r for part in parts for r in part]
        self.assertEqual(len(rows), 13 * 16 * 3)
        self.assertEqual(
            len({(name, rep, c["id"]) for name, rep, c in rows}), len(rows)
        )
        for name in names:
            self.assertEqual(sum(any(r[0] == name for r in part) for part in parts), 1)
        self.assertEqual(
            parts[0], list(schedule(self.plan, list(reversed(names)), 0, 4))
        )
        block_orders = [
            tuple(c["id"] for n, r, c in rows if n == names[0] and r == rep)
            for rep in range(3)
        ]
        self.assertGreater(len(set(block_orders)), 1)

    def test_timeout_and_post_answer_crash_are_never_successes(self):
        good = dict(status="ok", answer="sat", exit_code=0, timeout="false")
        check_run(good, "sat")
        with self.assertRaises(ValueError):
            check_run(good, "unsat")
        for run in [
            dict(good, status="timeout", timeout="true"),
            dict(good, status="error", exit_code=-11),
        ]:
            self.assertIsNone(decisive(run))
            self.assertEqual(
                model_groups([dict(run=run, configuration="x", repeat=0)]), {}
            )
        with self.assertRaises(ValueError):
            check_run(dict(good, status="error", exit_code=-11), "sat")

    def test_models_preserve_repetition_and_profile_identity(self):
        run = dict(status="ok", answer="sat", exit_code=0, timeout="false")
        rows = [
            dict(run=run, configuration=c, repeat=r)
            for c in ["a", "b"]
            for r in range(3)
        ]
        self.assertEqual(len(model_groups(rows)), 6)


if __name__ == "__main__":
    unittest.main()
