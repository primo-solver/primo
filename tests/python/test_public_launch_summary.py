"""Qualification reports must reject missing work and mixed evidence."""
import csv
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/qualification"))
from summarize_public_launch import ROUTES, aggregate, models


class PublicLaunchSummaryTests(unittest.TestCase):
    def shards(self):
        return [{"slot": i, "route": ROUTES[i // 4], "binaries": {"primo": "same"},
                 "cases": 250, "pass": 250, "timeout": 0, "sat_models": 0,
                 "queries": 0, "input_sha256": [f"{i}-{j}" for j in range(250)]}
                for i in range(32)]

    def test_missing_duplicate_and_mixed_binary_shards(self):
        shards = self.shards()
        for bad in (shards[:-1], shards[:-1] + [shards[0]],
                    shards[:-1] + [shards[-1] | {"binaries": {"primo": "other"}}]):
            with self.assertRaises(ValueError):
                aggregate(bad)

    def test_timeouts_are_separate(self):
        shards = self.shards()
        shards[8].update({"pass": 248, "timeout": 2})
        row = aggregate(shards)["routes"]["QF_LIA"]
        self.assertEqual((row["cases"], row["pass"], row["timeout"]), (1000, 998, 2))

    def test_overlapping_sessions_rejected(self):
        shards = self.shards()
        shards[25]["input_sha256"] = shards[24]["input_sha256"]
        with self.assertRaisesRegex(ValueError, "overlapping"):
            aggregate(shards)

    def test_model_coverage_and_exit_status(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            source = root / "input.smt2"
            target = root / "summary.csv"
            row = {"file": str(source), "outcome": "pass", "validator": "dolmen",
                   "solver_exit": "0", "dolmen_exit": "0"}

            def write(rows):
                with target.open("w") as stream:
                    writer = csv.DictWriter(stream, fieldnames=row)
                    writer.writeheader()
                    writer.writerows(rows)

            write([row])
            self.assertEqual(models(target, {source}), 1)
            for rows in ([], [row, row], [row | {"solver_exit": "1"}],
                         [row | {"file": str(root / "different.smt2")}],
                         [row | {"dolmen_exit": "1"}]):
                write(rows)
                with self.assertRaises(ValueError):
                    models(target, {source})
