import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from assemble_docs import assemble  # noqa: E402
from check_doc_links import Checker, Page  # noqa: E402


class AssembleDocsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.current = self.root / "current"
        self.history = self.root / "history"
        self.site = self.root / "site"
        for book in (self.current, self.history):
            book.mkdir()
            (book / "index.html").write_text('<html><body><h1 id="intro">Intro</h1></body></html>')
        self.manifest = {"routes": []}

    def route(self, uri):
        self.manifest["routes"].append({"old_path": uri, "archive_path": "archive/original/" + uri})

    def build(self):
        return assemble(self.current, self.history, self.site, self.manifest)

    def test_retired_pages_and_fragments_work_without_javascript(self):
        (self.history / "old.html").write_text('<h1 id="old-heading">Old</h1>')
        self.route("old.html")
        self.route("old.html#old-heading")
        report = self.build()
        self.assertEqual(report["redirect_pages"], 1)
        text = (self.site / "old.html").read_text()
        self.assertIn("location.replace", text)
        self.assertIn("old-heading", Page(text).ids)
        checker = Checker(self.root)
        checker.check_book(self.site)
        self.assertEqual(checker.failures, [])

    def test_current_anchor_wins_and_obsolete_anchor_routes_to_archive(self):
        (self.history / "index.html").write_text('<h1 id="intro">Intro</h1><h2 id="old">Old</h2>')
        for uri in ("index.html", "index.html#intro", "index.html#old"):
            self.route(uri)
        report = self.build()
        text = (self.site / "index.html").read_text()
        self.assertEqual(report["fragment_routes"], 1)
        self.assertEqual(Page(text).ids, {"intro", "old"})
        self.assertNotIn('"intro":', text)
        self.assertIn('"old": "archive/original/index.html#old"', text)
        self.assertEqual(json.loads((self.site / "compatibility-report.json").read_text()), report)

    def test_missing_archive_anchor_preserves_previous_site(self):
        self.site.mkdir()
        (self.site / "keep").write_text("previous")
        self.route("index.html#missing")
        with self.assertRaisesRegex(ValueError, "missing archived anchor"):
            self.build()
        self.assertEqual((self.site / "keep").read_text(), "previous")

    def test_repeated_build_does_not_accumulate_routes(self):
        self.route("index.html")
        self.build()
        (self.site / "stale.html").write_text("stale")
        self.build()
        self.assertFalse((self.site / "stale.html").exists())

    def test_reject_path_escape_and_overlapping_builds(self):
        self.route("../outside.html")
        with self.assertRaisesRegex(ValueError, "unsafe compatibility target"):
            self.build()
        with self.assertRaisesRegex(ValueError, "separate"):
            assemble(self.current, self.history, self.current, {"routes": []})


if __name__ == "__main__":
    unittest.main()
