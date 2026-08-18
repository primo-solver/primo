import importlib.util
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

    def test_manifest_excludes_original_documentation_and_repository_files(
        self,
    ) -> None:
        for path in ["README.md", "docs/index.md", "AGENTS.md", ".gitignore"]:
            with self.subTest(path=path):
                self.assertFalse(ARCHIVE.is_allowed_tracked_path(PurePosixPath(path)))

    def test_html_filter_excludes_sphinx_sources(self) -> None:
        for path in ["_sources/index.md", "papers/source.markdown", ".git/config"]:
            with self.subTest(path=path):
                self.assertTrue(ARCHIVE.is_forbidden_archive_path(PurePosixPath(path)))
        self.assertFalse(ARCHIVE.is_forbidden_archive_path(PurePosixPath("index.html")))

    def test_release_readme_records_revision_and_expected_samples(self) -> None:
        readme = ARCHIVE.release_readme("abc123").decode("utf-8")
        self.assertIn("Revision: abc123", readme)
        self.assertIn("expected answers are sat, unsat, and unsat", readme)


if __name__ == "__main__":
    unittest.main()
