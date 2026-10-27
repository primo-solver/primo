import importlib.util
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path, PurePosixPath

SCRIPT = Path(__file__).parents[2] / "scripts" / "create_source_archive.py"
SPEC = importlib.util.spec_from_file_location("create_source_archive", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
ARCHIVE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ARCHIVE)


class SourceArchiveTests(unittest.TestCase):
    def test_manifest_includes_code_and_smt_inputs(self) -> None:
        self.assertTrue(ARCHIVE.is_allowed_tracked_path(PurePosixPath("src/main.cpp")))
        self.assertTrue(
            ARCHIVE.is_allowed_tracked_path(PurePosixPath("scripts/run_smtlib.py"))
        )
        self.assertTrue(
            ARCHIVE.is_allowed_tracked_path(PurePosixPath("benchmarks/smoke/a.smt2"))
        )

    def test_manifest_includes_editable_documentation_and_repository_files(
        self,
    ) -> None:
        for path in ["README.md", "docs/index.md", "AGENTS.md", ".gitignore"]:
            with self.subTest(path=path):
                self.assertTrue(ARCHIVE.is_allowed_tracked_path(PurePosixPath(path)))

    def test_html_filter_excludes_sphinx_sources(self) -> None:
        for path in ["_sources/index.md", "papers/reading.pdf", ".git/config", "docs/_build/cache"]:
            with self.subTest(path=path):
                self.assertTrue(ARCHIVE.is_forbidden_archive_path(PurePosixPath(path)))
        self.assertFalse(ARCHIVE.is_forbidden_archive_path(PurePosixPath("index.html")))
        self.assertFalse(ARCHIVE.is_forbidden_archive_path(PurePosixPath("docs/index.md")))

    def test_release_readme_records_revision_and_expected_samples(self) -> None:
        readme = ARCHIVE.release_readme("abc123").decode("utf-8")
        self.assertIn("Revision: abc123", readme)
        self.assertIn("expected answers are sat, unsat, and unsat", readme)

    def test_archive_reproducibility_editable_books_and_private_exclusion(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            def git(*args):
                return subprocess.check_output(["git", "-C", str(repo), *args], stderr=subprocess.PIPE)
            git("init", "-q")
            for name in ["README.md", "docs/index.md", "docs-history/index.md", "benchmarks/manifest.json", "external/reading.pdf"]:
                path = repo / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture\n")
            (repo / ".gitignore").write_text("docs/_build/\ndist/\n")
            git("add", ".")
            git("-c", "user.name=Archive Test", "-c", "user.email=archive@example.invalid", "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")
            site = repo / "docs/_build/site"
            site.mkdir(parents=True)
            (site / "index.html").write_text("<h1>Manual</h1>")
            archive, _ = ARCHIVE.create_archive(repo, repo / "dist", False)
            first = archive.read_bytes()
            second, _ = ARCHIVE.create_archive(repo, repo / "dist", False)
            self.assertEqual(first, second.read_bytes())
            with tarfile.open(archive) as opened:
                names = [name.split("/", 1)[1] for name in opened.getnames()]
            for required in ["docs/index.md", "docs-history/index.md", "README.md", "SOURCE-REVISION", "html/index.html", "benchmarks/manifest.json"]:
                self.assertIn(required, names)
            self.assertNotIn("external/reading.pdf", names)


if __name__ == "__main__":
    unittest.main()
