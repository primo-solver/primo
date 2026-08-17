import unittest

from scripts.compare_local_solver_stats import parse_statistics, summarize


class CompareLocalSolverStatsTests(unittest.TestCase):
    def test_parse_statistics_excludes_only_time_counters(self) -> None:
        stats, diagnostics = parse_statistics(
            ":sat.decisions 12\n:time.total 1.25\nwarning text\n:theory.checks 4\n"
        )

        self.assertEqual(stats, {":sat.decisions": "12", ":theory.checks": "4"})
        self.assertEqual(diagnostics, ["warning text"])

    def test_summary_requires_answers_and_non_time_statistics_to_match(self) -> None:
        rows = [
            {
                "file": "case.smt2",
                "solver": "baseline",
                "status": "ok",
                "answer": "sat",
                "seconds": 2.0,
                "stats": {":sat.decisions": "12"},
            },
            {
                "file": "case.smt2",
                "solver": "candidate",
                "status": "ok",
                "answer": "sat",
                "seconds": 2.1,
                "stats": {":sat.decisions": "13"},
            },
        ]

        summary = summarize(rows, ["case.smt2"], repeats=1)

        self.assertTrue(summary["all_answers_match"])
        self.assertFalse(summary["all_non_time_stats_match"])
        self.assertEqual(summary["baseline_median_sum_seconds"], 2.0)
        self.assertEqual(summary["candidate_median_sum_seconds"], 2.1)


if __name__ == "__main__":
    unittest.main()
