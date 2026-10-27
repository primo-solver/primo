"""Static-analysis reports must distinguish findings from incomplete execution."""

import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"scripts/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


SUMMARY = load_script("summarize_static_analysis")
HEADERS = load_script("check_static_headers")


class StaticReportingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name) / "source with spaces"
        self.repo.mkdir()
        self.files = ["src/example.cpp", "tests/unit/test.cpp", "src/example.hpp"]
        for name in self.files:
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// fixture\n")
        self.manifest = {
            "source_root": str(self.repo),
            "source_sha256": {
                name: hashlib.sha256((self.repo / name).read_bytes()).hexdigest()
                for name in self.files
            },
        }
        self.log = "".join(
            f"/usr/bin/clang-tidy-18 -p database {self.repo / name}\n"
            for name in self.files[:2]
        )

    def test_success_and_complete_coverage_are_required(self):
        self.assertEqual(
            SUMMARY.summarize(self.manifest, self.log, 0)["status"], "clean"
        )
        self.assertEqual(
            SUMMARY.summarize(self.manifest, self.log, 1)["status"], "incomplete"
        )
        report = SUMMARY.summarize(self.manifest, self.log.splitlines()[0] + "\n", 0)
        self.assertEqual(report["status"], "incomplete")
        self.assertEqual(report["missing"], [self.files[1]])

    def test_header_diagnostics_are_deduplicated_and_visible(self):
        warning = f"{self.repo / self.files[2]}:1:2: warning: questionable copy [performance-test]\n"
        report = SUMMARY.summarize(self.manifest, self.log + warning * 2, 0)
        self.assertEqual(report["status"], "complete-with-findings")
        self.assertEqual(report["unique_diagnostics"], 1)
        self.assertEqual(report["diagnostic_occurrences"], 2)
        self.assertEqual(report["diagnostics"][0]["file"], self.files[2])

    def test_unbracketed_compiler_errors_are_failures(self):
        error = f"{self.repo / self.files[0]}:1:2: fatal error: missing header\n"
        self.assertEqual(
            SUMMARY.summarize(self.manifest, self.log + error, 0)["status"],
            "incomplete",
        )

    def test_changed_or_deleted_sources_fail_even_if_tool_succeeded(self):
        (self.repo / self.files[0]).write_text("// changed\n")
        (self.repo / self.files[2]).unlink()
        report = SUMMARY.summarize(self.manifest, self.log, 0)
        self.assertEqual(report["status"], "incomplete")
        self.assertEqual(set(report["changed_inputs"]), {self.files[0], self.files[2]})

    def test_changed_generated_revision_header_fails(self):
        header = self.repo / "revision.hpp"
        header.write_text("revision1")
        self.manifest["generated_revision_header"] = {
            "path": str(header),
            "sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
        }
        header.write_text("revision2")
        report = SUMMARY.summarize(self.manifest, self.log, 0)
        self.assertEqual(report["status"], "incomplete")

    def test_unknown_source_command_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unrecognized analysis command"):
            SUMMARY.summarize(
                self.manifest, self.log + "clang-tidy-18 /other/source.cpp\n", 0
            )

    def test_header_command_preserves_flags_without_object_generation(self):
        source = str(self.repo / self.files[0])
        entry = {
            "directory": str(self.repo),
            "file": source,
            "arguments": [
                "clang++-18",
                "-std=c++20",
                "-Ipath with spaces",
                "-DPRIMO_TEST=1",
                "-o",
                "object.o",
                "-c",
                source,
            ],
        }
        generated = self.repo / "header.cpp"
        command = HEADERS.syntax_command(entry, generated)
        self.assertEqual(
            command,
            [
                "clang++-18",
                "-std=c++20",
                "-Ipath with spaces",
                "-DPRIMO_TEST=1",
                "-fsyntax-only",
                "-x",
                "c++",
                str(generated),
            ],
        )


if __name__ == "__main__":
    unittest.main()
