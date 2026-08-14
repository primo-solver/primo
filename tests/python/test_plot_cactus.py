import csv
import tempfile
import unittest
from pathlib import Path

from scripts.plot_cactus import load_series, svg

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


if __name__ == "__main__":
    unittest.main()
