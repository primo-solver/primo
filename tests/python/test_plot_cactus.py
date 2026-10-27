import csv
import tempfile
import unittest
from pathlib import Path

from scripts.plot_cactus import (
    answered,
    load_series,
    order_series_by_solved,
    summarize,
    svg,
)

FIELDS = ["file", "status", "seconds", "wrong", "answer", "stderr_head"]


def write_rows(path: Path, files: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        for file in files:
            writer.writerow(
                {
                    "file": file,
                    "status": "ok",
                    "seconds": "0.1",
                    "wrong": "false",
                    "answer": "sat",
                    "stderr_head": "",
                }
            )


class PlotCactusTests(unittest.TestCase):
    def test_loads_directory_scoped_shards_and_skips_summaries(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_rows(root / "shard-0.csv", ["a.smt2"])
            write_rows(root / "shard-1.csv", ["b.smt2"])
            write_rows(root / "shard-1-summary.csv", ["summary"])

            rows = load_series(root, "Primo all")

            self.assertEqual({row["file"] for row in rows}, {"a.smt2", "b.smt2"})

    def test_named_layout_takes_precedence_over_generic_shards(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_rows(root / "primo.shard-0.csv", ["named.smt2"])
            write_rows(root / "shard-0.csv", ["generic.smt2"])

            rows = load_series(root, "primo")

            self.assertEqual([row["file"] for row in rows], ["named.smt2"])

    def test_svg_escapes_user_supplied_text(self) -> None:
        document = svg(
            [("solver <one>", [0.1])],
            timeout=1,
            total=1,
            title="A & B",
            caption=["source <artifact>"],
        )

        self.assertIn("A &amp; B", document)
        self.assertIn("solver &lt;one&gt;", document)
        self.assertIn("source &lt;artifact&gt;", document)

    def test_orders_by_solved_count_then_name(self) -> None:
        ordered = order_series_by_solved(
            [("zeta", [0.1]), ("beta", [0.1, 0.2]), ("alpha", [0.1, 0.2])]
        )

        self.assertEqual([name for name, _ in ordered], ["alpha", "beta", "zeta"])

    def test_highlight_uses_prominent_blue_and_heavier_line(self) -> None:
        document = svg(
            [("Primo all", [0.1]), ("reference", [0.2])],
            timeout=1,
            total=1,
            title="QF_UFLRA",
            caption=[],
            highlighted="Primo all",
        )

        self.assertIn('stroke="#0057b8" stroke-width="3.4"', document)
        self.assertIn('font-weight="600">Primo all', document)


if __name__ == "__main__":
    unittest.main()


class SolvedDefinitionTests(unittest.TestCase):
    """A clean exit is not a decision.

    primo returns status `ok` with answer `unknown` when QF_LIA
    branch-and-bound exhausts its node limit -- 750 files per arm on the
    13,306-file division, against zero for cvc5 and z3. Scoring `status ==
    ok` put primo's curve 750 instances above the truth.
    """

    def rows(self) -> list[dict[str, str]]:
        def row(name, status, answer, seconds):
            return {"file": name, "status": status, "answer": answer,
                    "seconds": seconds, "wrong": "false", "stderr_head": ""}
        return [
            row("sat.smt2", "ok", "sat", "1.0"),
            row("unsat.smt2", "ok", "unsat", "2.0"),
            row("nodelimit.smt2", "ok", "unknown", "0.5"),
            row("timeout.smt2", "timeout", "unknown", "60.0"),
        ]

    def test_clean_unknown_is_not_solved(self) -> None:
        self.assertFalse(answered(self.rows()[2]))
        stats = summarize(self.rows(), timeout=60.0)
        self.assertEqual(stats["solved"], [1.0, 2.0])
        self.assertEqual(stats["clean_unknown"], 1)

    def test_clean_unknown_is_penalised_in_par2(self) -> None:
        stats = summarize(self.rows(), timeout=60.0)
        # 1.0 + 2.0 solved, and BOTH non-answers charged 2T -- the node-limit
        # exit must not be charged its own 0.5s just for exiting cleanly.
        self.assertEqual(stats["par2"], 1.0 + 2.0 + 2 * 60.0 * 2)

    def test_a_timeout_is_still_not_solved(self) -> None:
        self.assertFalse(answered(self.rows()[3]))


class LegendBreakdownTests(unittest.TestCase):
    """The sat/unsat split belongs in the legend, not just the console table.

    The two halves move independently -- QF_LIA patching is a sat-side
    mechanism -- so a pooled solved count can hide the whole effect of a
    change.
    """

    def test_summarize_splits_sat_and_unsat(self) -> None:
        rows = [
            {"file": "a", "status": "ok", "answer": "sat", "seconds": "1",
             "wrong": "false", "stderr_head": ""},
            {"file": "b", "status": "ok", "answer": "unsat", "seconds": "2",
             "wrong": "false", "stderr_head": ""},
            {"file": "c", "status": "ok", "answer": "unknown", "seconds": "3",
             "wrong": "false", "stderr_head": ""},
        ]
        stats = summarize(rows, timeout=60.0)
        self.assertEqual((stats["sat"], stats["unsat"]), (1, 1))
        self.assertEqual(len(stats["solved"]), 2)

    def test_legend_shows_the_split_when_given_one(self) -> None:
        out = svg([("primo", [1.0, 2.0, 3.0])], timeout=60.0, total=10,
                  title="t", caption=["c"], breakdown={"primo": (2, 1)})
        self.assertIn("primo", out)
        self.assertIn("2 sat", out)
        self.assertIn("1 unsat", out)

    def test_legend_without_a_breakdown_is_unchanged(self) -> None:
        out = svg([("primo", [1.0])], timeout=60.0, total=10,
                  title="t", caption=["c"])
        self.assertNotIn("sat &#183;", out)
