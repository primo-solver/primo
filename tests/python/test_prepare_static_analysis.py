"""Reject incomplete/stale static-analysis coverage without building a solver."""

import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "prepare_static", ROOT / "scripts/prepare_static_analysis.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class StaticDatabaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name) / "repository with spaces"
        self.repo.mkdir()
        self.build = self.repo / "build"
        self.build.mkdir()
        self.out = self.repo / "analysis"
        files = [
            "src/util/rational.cpp",
            "src/smt/solver_engines.cpp",
            "tests/unit/example.cpp",
            "tests/bench/word_gcd_bench.cpp",
            "docs/examples/example.cpp",
            "src/util/rational.hpp",
        ]
        for name in files:
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// fixture\n")
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        subprocess.run(["git", "-C", str(self.repo), "add", "."], check=True)
        self.entries = [
            {
                "directory": str(self.build),
                "file": str(self.repo / name),
                "arguments": [
                    "clang++",
                    "-std=c++20",
                    "-DPRIMO_HAVE_IPASIR=1",
                    "-c",
                    str(self.repo / name),
                    "-o",
                    "object.o",
                ],
            }
            for name in files[:3]
        ]
        self.write_database()

    def write_database(self):
        (self.build / "compile_commands.json").write_text(json.dumps(self.entries))

    def test_all_sources_have_explicit_commands_and_header_hashes(self):
        report = MODULE.prepare(self.repo, self.build, self.out)
        self.assertEqual(report["translation_units"], 5)
        self.assertEqual(len(report["derived_commands"]), 2)
        self.assertIn("src/util/rational.hpp", report["source_sha256"])
        rows = json.loads((self.out / "compile_commands.json").read_text())
        for row in rows:
            self.assertIn(row["file"], row["arguments"])
            self.assertIn("-DPRIMO_HAVE_IPASIR=1", row["arguments"])

    def test_missing_unit_test_command_is_rejected(self):
        self.entries.pop()
        self.write_database()
        with self.assertRaisesRegex(ValueError, "missing production/unit-test"):
            MODULE.prepare(self.repo, self.build, self.out)

    def test_stale_source_argument_is_rejected(self):
        self.entries[0]["arguments"][4] = "/old-checkout/src/util/rational.cpp"
        self.write_database()
        with self.assertRaisesRegex(ValueError, "cannot identify source argument"):
            MODULE.prepare(self.repo, self.build, self.out)

    def test_multiple_flag_variants_are_preserved(self):
        second = {
            **self.entries[0],
            "arguments": [*self.entries[0]["arguments"], "-DVARIANT=1"],
        }
        self.entries.append(second)
        self.write_database()
        report = MODULE.prepare(self.repo, self.build, self.out)
        self.assertEqual(report["translation_units"], 5)
        self.assertEqual(report["compile_commands"], 7)

    def test_build_database_cannot_be_overwritten(self):
        before = (self.build / "compile_commands.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "separate from the build"):
            MODULE.prepare(self.repo, self.build, self.build)
        self.assertEqual((self.build / "compile_commands.json").read_bytes(), before)

    def test_existing_attempt_cannot_be_overwritten(self):
        MODULE.prepare(self.repo, self.build, self.out)
        before = (self.out / "manifest.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "already exists"):
            MODULE.prepare(self.repo, self.build, self.out)
        self.assertEqual((self.out / "manifest.json").read_bytes(), before)

    def test_main_requires_generated_revision_metadata(self):
        source = self.repo / "src/main.cpp"
        source.write_text("// fixture\n")
        subprocess.run(["git", "-C", str(self.repo), "add", str(source)], check=True)
        self.entries.append(
            {
                **self.entries[0],
                "file": str(source),
                "arguments": [
                    "clang++",
                    "-std=c++20",
                    "-c",
                    str(source),
                    "-o",
                    "main.o",
                ],
            }
        )
        self.write_database()
        with self.assertRaisesRegex(ValueError, "missing generated revision header"):
            MODULE.prepare(self.repo, self.build, self.out)
        header = self.build / "generated/primo_git_revision.hpp"
        header.parent.mkdir()
        header.write_text('#define PRIMO_GIT_REVISION "fixture"\n')
        report = MODULE.prepare(self.repo, self.build, self.out)
        self.assertEqual(report["generated_revision_header"]["path"], str(header))

    def test_make_tidy_does_not_build_or_execute_solver_targets(self):
        result = subprocess.run(
            ["make", "-n", "tidy"], cwd=ROOT, text=True, capture_output=True, check=True
        )
        self.assertIn("prepare_static_analysis.py", result.stdout)
        self.assertNotIn("cmake --build", result.stdout)
        self.assertNotIn("contrib-required", result.stdout)
        self.assertNotIn("ctest", result.stdout)


if __name__ == "__main__":
    unittest.main()
