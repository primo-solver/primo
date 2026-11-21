import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts" / "check_doc_links.py"
SPEC = importlib.util.spec_from_file_location("check_doc_links", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
LINKS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LINKS)


class DocLinkTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.book = self.root / "html"
        self.book.mkdir()
        self.index = self.book / "index.html"
        self.index.write_text("<h1 id='intro'>Introduction</h1>")

    def check(self, html):
        self.index.write_text(html)
        checker = LINKS.Checker(self.root)
        checker.check_book(self.book)
        return checker

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-C", str(self.root), *args], stderr=subprocess.PIPE, text=True
        ).strip()

    def test_local_targets_anchors_and_url_encoding(self):
        (self.book / "a b.html").write_text(
            '<h1 id="déjà">Heading</h1><a name="old"></a>'
        )
        (self.book / "asset.svg").write_text("<svg/>")
        (self.book / "nested").mkdir()
        (self.book / "nested/index.html").write_text("<h1 id='nested'>Nested</h1>")
        checker = self.check("""<h1 id="intro">Intro</h1>
            <a href="#intro">same page</a>
            <a href="a%20b.html?view=1#d%C3%A9j%C3%A0">encoded</a>
            <a href="a%20b.html#old">legacy anchor</a>
            <a href="/nested/#nested">root relative directory</a>
            <img src="asset.svg?version=1">
            <a href="https://example.org/#remote">external</a>""")
        self.assertEqual(checker.failures, [])
        self.assertEqual(checker.local_links, 5)
        self.assertEqual(checker.external_urls, {"https://example.org/#remote"})

    def test_missing_page_asset_and_fragment_fail(self):
        checker = self.check("""<a href="missing.html">missing</a>
            <img src="missing.svg"><a href="#absent">anchor</a>""")
        self.assertEqual(len(checker.failures), 3)
        self.assertIn("missing HTML anchor", checker.failures[-1]["error"])

    def test_unlinked_source_paths_in_article_prose_fail(self):
        checker = self.check("""<article>
            <p>Builders: <code>src/smt/strategy.hpp</code>.</p>
            <p>Implementation: src/smt/tactic.cpp.</p>
            <p><a id="label">src/core/term.hpp</a></p>
            </article>""")
        self.assertEqual(
            [(failure["line"], failure["uri"]) for failure in checker.failures],
            [
                (2, "src/smt/strategy.hpp"),
                (3, "src/smt/tactic.cpp"),
                (4, "src/core/term.hpp"),
            ],
        )

    def test_source_links_code_blocks_and_navigation_are_not_omissions(self):
        checker = self.check("""<nav>src/core/term.hpp</nav><article>
            <a href="#owner"><code><span>src/smt/strategy.hpp</span></code></a>
            <pre><code>clang-format src/smt/tactic.cpp</code></pre>
            <script>const file = "src/core/term.hpp";</script>
            <style>/* src/core/term.hpp */</style>
            <h2 id="owner">Owner</h2><p><code>src/smt/</code> is a layer.</p>
            <p>Use <code>src/smt/*.hpp</code> for a shell glob.</p></article>""")
        self.assertEqual(checker.failures, [])
        self.assertEqual(checker.local_links, 1)

    def test_historical_prose_is_preserved_but_its_links_are_checked(self):
        archive = self.book / "archive/original"
        archive.mkdir(parents=True)
        (archive / "old.html").write_text("""<article>
            <code>src/smt/strategy.hpp</code><a href="missing.html">broken</a>
            </article>""")
        checker = self.check("<article>Current prose.</article>")
        self.assertEqual(len(checker.failures), 1)
        self.assertEqual(checker.failures[0]["error"], "local target does not exist")

    def test_cannot_escape_book_even_if_file_exists(self):
        (self.root / "outside.html").write_text("outside")
        checker = self.check('<a href="../outside.html">escape</a>')
        self.assertIn("escapes", checker.failures[0]["error"])

    def test_missing_build_and_missing_git_commit_fail(self):
        checker = LINKS.Checker(self.root)
        checker.check_book(self.root / "absent")
        self.assertEqual(len(checker.failures), 1)
        error = checker.check_git(
            "https://github.com/primo-solver/primo/blob/abcdef0/src/a.cpp#L1"
        )
        self.assertIn("unavailable", error)

    def test_theme_templates_are_not_rendered_pages(self):
        (self.book / "_static").mkdir()
        (self.book / "_static/template.html").write_text('<a href="{{ template() }}">')
        checker = self.check('<a href="_static/template.html">asset</a>')
        self.assertEqual(checker.failures, [])
        self.assertEqual(checker.pages, 1)

    def test_git_uses_pinned_content_and_checks_ranges(self):
        git = self.git
        git("init", "-q")
        (self.root / "source.cpp").write_text("first\nsecond\n")
        git("add", "source.cpp")
        git(
            "-c",
            "user.name=Link Test",
            "-c",
            "user.email=links@example.invalid",
            "-c",
            "commit.gpgsign=false",
            "commit",
            "-qm",
            "fixture",
        )
        revision = git("rev-parse", "HEAD")
        (self.root / "source.cpp").write_text("changed\n")
        checker = LINKS.Checker(self.root)
        base = f"https://github.com/primo-solver/primo/blob/{revision}/source.cpp"
        self.assertIsNone(checker.check_git(base + "#L2"))
        self.assertIsNone(checker.check_git(base + "#L1-L2"))
        for fragment in ("#L0", "#L3", "#L2-L1", "#heading"):
            with self.subTest(fragment=fragment):
                self.assertIsNotNone(checker.check_git(base + fragment))
        self.assertIsNotNone(
            checker.check_git(base.replace("source.cpp", "missing.cpp"))
        )
        self.assertIsNotNone(checker.check_git(base.replace(revision, "main")))
        self.assertIsNone(
            checker.check_git(
                f"https://github.com/primo-solver/primo/commit/{revision}"
            )
        )

    def test_git_rejects_unrelated_history_present_in_local_database(self):
        git = self.git
        git("init", "-q")
        (self.root / "source.cpp").write_text("base\n")
        git("add", "source.cpp")
        identity = (
            "-c",
            "user.name=Link Test",
            "-c",
            "user.email=links@example.invalid",
            "-c",
            "commit.gpgsign=false",
        )
        git(*identity, "commit", "-qm", "base")
        ancestor = git("rev-parse", "HEAD")
        git("checkout", "-qb", "local-only")
        (self.root / "source.cpp").write_text("side history\n")
        git(*identity, "commit", "-qam", "local-only snapshot")
        unrelated = git("rev-parse", "HEAD")
        git("checkout", "-q", "--detach", ancestor)
        (self.root / "source.cpp").write_text("candidate\n")
        git(*identity, "commit", "-qam", "candidate")
        # The side branch keeps the object locally available, unlike a CI clone
        # that fetches only this candidate and the upstream repository's refs.
        self.assertEqual(git("cat-file", "-t", unrelated), "commit")
        checker = LINKS.Checker(self.root)
        for suffix in (
            f"blob/{unrelated}/source.cpp#L1",
            f"commit/{unrelated}",
            f"blob/{unrelated[:8]}/source.cpp#L1",
        ):
            with self.subTest(suffix=suffix):
                self.assertIn(
                    "not reachable from HEAD",
                    checker.check_git(
                        "https://github.com/primo-solver/primo/" + suffix
                    ),
                )
        for revision in (ancestor, git("rev-parse", "HEAD")):
            with self.subTest(revision=revision):
                self.assertIsNone(
                    checker.check_git(
                        f"https://github.com/primo-solver/primo/blob/{revision}/source.cpp#L1"
                    )
                )

    def archived_history(self):
        git = self.git
        git("init", "-q")
        identity = (
            "-c",
            "user.name=Link Test",
            "-c",
            "user.email=links@example.invalid",
            "-c",
            "commit.gpgsign=false",
        )
        (self.root / "source.cpp").write_text("base\n")
        git("add", "source.cpp")
        git(*identity, "commit", "-qm", "base")
        base = git("rev-parse", "HEAD")
        git("checkout", "-qb", "development")
        (self.root / "source.cpp").write_text("archived\nsecond line\n")
        git(*identity, "commit", "-qam", "development source")
        archived = git("rev-parse", "HEAD")
        git(
            *identity,
            "-c",
            "tag.gpgsign=false",
            "tag",
            "-am",
            "source archive",
            "source-history/test",
        )
        git("checkout", "-qb", "candidate", base)
        (self.root / "source.cpp").write_text("squashed source\n")
        git(*identity, "commit", "-qam", "candidate")
        manifest = self.root / "docs/source-history.json"
        manifest.parent.mkdir()
        data = {
            "schema_version": 1,
            "archives": [
                {
                    "repository": "https://github.com/primo-solver/primo.git",
                    "ref": "refs/tags/source-history/test",
                    "commit": archived,
                }
            ],
        }
        manifest.write_text(json.dumps(data))
        return base, archived, manifest, data

    def test_pinned_canonical_archive_accepts_only_its_history(self):
        base, archived, _, _ = self.archived_history()
        checker = LINKS.Checker(self.root)
        self.assertEqual(checker.failures, [])
        for revision in (base, archived, archived[:8], self.git("rev-parse", "HEAD")):
            with self.subTest(revision=revision):
                self.assertIsNone(
                    checker.check_git(
                        f"https://github.com/primo-solver/primo/blob/{revision}/source.cpp#L1"
                    )
                )
        self.assertIsNone(
            checker.check_git(
                f"https://github.com/primo-solver/primo/blob/{archived}/source.cpp#L2"
            )
        )
        self.assertIsNotNone(
            checker.check_git(
                f"https://github.com/primo-solver/primo/blob/{archived}/source.cpp#L3"
            )
        )
        self.git("checkout", "-qb", "unrelated", base)
        (self.root / "source.cpp").write_text("unrelated\n")
        self.git(
            "-c",
            "user.name=Link Test",
            "-c",
            "user.email=links@example.invalid",
            "-c",
            "commit.gpgsign=false",
            "commit",
            "-qam",
            "unrelated",
        )
        unrelated = self.git("rev-parse", "HEAD")
        self.git("checkout", "-q", "candidate")
        checker = LINKS.Checker(self.root)
        self.assertIn(
            "not reachable",
            checker.check_git(
                f"https://github.com/primo-solver/primo/commit/{unrelated}"
            ),
        )

    def test_missing_or_moved_archive_tag_fails_despite_existing_objects(self):
        _, archived, _, _ = self.archived_history()
        self.git("tag", "-d", "source-history/test")
        self.assertEqual(self.git("cat-file", "-t", archived), "commit")
        checker = LINKS.Checker(self.root)
        self.assertIn(
            "tag is missing",
            checker.check_git(
                f"https://github.com/primo-solver/primo/commit/{archived}"
            ),
        )
        self.assertEqual(len(checker.failures), 1)
        self.git("-c", "tag.gpgsign=false", "tag", "source-history/test", "HEAD")
        checker = LINKS.Checker(self.root)
        self.assertIn(
            "expected",
            checker.check_git(
                f"https://github.com/primo-solver/primo/commit/{archived}"
            ),
        )

    def test_archive_manifest_rejects_forks_and_invalid_configuration(self):
        _, archived, manifest, original = self.archived_history()
        uri = f"https://github.com/primo-solver/primo/commit/{archived}"
        for field, value in (
            ("repository", "https://github.com/example/primo.git"),
            ("ref", "refs/heads/development"),
            ("ref", "refs/tags/../HEAD"),
            ("commit", archived[:8]),
        ):
            with self.subTest(field=field, value=value):
                data = json.loads(json.dumps(original))
                data["archives"][0][field] = value
                manifest.write_text(json.dumps(data))
                checker = LINKS.Checker(self.root)
                self.assertIn(
                    "invalid canonical source history", checker.check_git(uri)
                )
        for value in (
            "{",
            "[]",
            '{"schema_version":2,"archives":[]}',
            '{"schema_version":1,"archives":{}}',
        ):
            with self.subTest(value=value):
                manifest.write_text(value)
                self.assertIn(
                    "invalid canonical source history",
                    LINKS.Checker(self.root).check_git(uri),
                )

    def test_clean_clone_requires_explicit_archive_fetch(self):
        _, archived, _, _ = self.archived_history()
        self.index.write_text(
            f'<a href="https://github.com/primo-solver/primo/blob/{archived}/source.cpp#L2">old</a>'
        )
        self.git("add", "docs/source-history.json", "html/index.html")
        self.git(
            "-c",
            "user.name=Link Test",
            "-c",
            "user.email=links@example.invalid",
            "-c",
            "commit.gpgsign=false",
            "commit",
            "-qm",
            "archive declaration",
        )
        clone = self.root / "clean-clone"
        subprocess.run(
            [
                "git",
                "clone",
                "--quiet",
                "--single-branch",
                "--no-tags",
                "--branch",
                "candidate",
                self.root.as_uri(),
                str(clone),
            ],
            check=True,
            capture_output=True,
        )
        with self.assertRaises(subprocess.CalledProcessError):
            subprocess.check_output(
                ["git", "-C", str(clone), "cat-file", "-t", archived],
                stderr=subprocess.PIPE,
            )
        checker = LINKS.Checker(clone)
        checker.check_book(clone / "html")
        self.assertEqual(len(checker.failures), 1)
        self.assertIn("tag is missing", checker.failures[0]["error"])
        subprocess.run(
            [
                "git",
                "-C",
                str(clone),
                "fetch",
                "--quiet",
                "--no-tags",
                self.root.as_uri(),
                "refs/tags/source-history/test:refs/tags/source-history/test",
            ],
            check=True,
            capture_output=True,
        )
        checker = LINKS.Checker(clone)
        checker.check_book(clone / "html")
        self.assertEqual(checker.failures, [])

    def test_cli_fails_and_preserves_json_report(self):
        self.index.write_text("""<article><a href="absent.html">broken</a>
            <code>src/smt/strategy.hpp</code></article>""")
        report = self.root / "report.json"
        result = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(self.book),
                "--repo",
                str(self.root),
                "--report",
                str(report),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(json.loads(report.read_text())["failures"]), 2)
        self.assertIn("absent.html", result.stdout)
        self.assertIn("unlinked source filename", result.stdout)


if __name__ == "__main__":
    unittest.main()
