import csv
import tempfile
import unittest
from pathlib import Path

from scripts.summarize_benchmark_shards import compare, load_rows, summarize


FIELDS = ["file", "family", "status", "answer", "wrong", "seconds"]


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


class SummarizeBenchmarkShardsTests(unittest.TestCase):
    def test_aggregate_and_compare(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            baseline_dir = root / "baseline"
            candidate_dir = root / "candidate"
            baseline_dir.mkdir()
            candidate_dir.mkdir()
            baseline_rows = [
                {
                    "file": "a.smt2",
                    "family": "A",
                    "status": "ok",
                    "answer": "sat",
                    "wrong": "false",
                    "seconds": "1.5",
                },
                {
                    "file": "b.smt2",
                    "family": "B",
                    "status": "timeout",
                    "answer": "unknown",
                    "wrong": "false",
                    "seconds": "5.0",
                },
            ]
            candidate_rows = [
                baseline_rows[0],
                {
                    "file": "b.smt2",
                    "family": "B",
                    "status": "ok",
                    "answer": "unsat",
                    "wrong": "false",
                    "seconds": "2.0",
                },
            ]
            write_rows(baseline_dir / "shard-0.csv", baseline_rows[:1])
            write_rows(baseline_dir / "shard-1.csv", baseline_rows[1:])
            write_rows(candidate_dir / "shard-0.csv", candidate_rows)
            write_rows(candidate_dir / "shard-0-summary.csv", [])

            baseline = load_rows(baseline_dir)
            candidate = load_rows(candidate_dir)
            baseline_summary = summarize(baseline, 5.0)
            comparison = compare(baseline, candidate)

            self.assertEqual(baseline_summary["files"], 2)
            self.assertEqual(baseline_summary["solved"], 1)
            self.assertEqual(baseline_summary["timeout"], 1)
            self.assertEqual(baseline_summary["par2"], 11.5)
            self.assertEqual(baseline_summary["families"]["B"]["timeout"], 1)
            self.assertEqual(comparison["recoveries"], ["b.smt2"])
            self.assertEqual(comparison["losses"], [])
            self.assertEqual(comparison["decisive_answer_disagreements"], [])

    def test_duplicate_rows_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            row = {
                "file": "a.smt2",
                "family": "A",
                "status": "ok",
                "answer": "sat",
                "wrong": "false",
                "seconds": "1.0",
            }
            write_rows(root / "shard-0.csv", [row])
            write_rows(root / "shard-1.csv", [row])
            with self.assertRaisesRegex(ValueError, "duplicate benchmark row"):
                load_rows(root)


if __name__ == "__main__":
    unittest.main()
