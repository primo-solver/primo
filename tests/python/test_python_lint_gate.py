"""Exercise mandatory Python lint against permission and import regressions."""

import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUFF = shlex.split(os.environ.get("RUFF", f"{ROOT}/.venv/bin/python -m ruff"))


class PythonLintGateTests(unittest.TestCase):
    def lint(self, source, executable=False):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "example.py"
            path.write_text(source)
            path.chmod(0o755 if executable else 0o644)
            result = subprocess.run(
                [*RUFF, "check", "--config", str(ROOT / "ruff.toml"),
                 "--output-format", "json", str(path)],
                capture_output=True, text=True, check=False, timeout=30,
            )
            self.assertIn(result.returncode, (0, 1), result.stderr)
            return result.returncode, {row["code"] for row in json.loads(result.stdout)}

    def test_shebang_requires_executable_file(self):
        self.assertEqual(self.lint("#!/usr/bin/env python3\nprint('ok')\n"),
                         (1, {"EXE001"}))

    def test_import_order_is_checked(self):
        self.assertEqual(self.lint("import sys\nimport os\n\nprint(sys.version, os.name)\n"),
                         (1, {"I001"}))

    def test_subprocess_run_requires_explicit_check(self):
        self.assertEqual(self.lint("import subprocess\n\nsubprocess.run(['example'])\n"),
                         (1, {"PLW1510"}))

    def test_executable_script_with_sorted_imports_passes(self):
        self.assertEqual(self.lint(
            "#!/usr/bin/env python3\nimport os\nimport sys\n\nprint(sys.version, os.name)\n",
            executable=True), (0, set()))

    def test_make_lint_rejects_missing_ruff(self):
        with tempfile.TemporaryDirectory() as directory:
            formatter = Path(directory) / "clang-format"
            formatter.write_text("#!/bin/sh\nexit 0\n")
            formatter.chmod(0o755)
            result = subprocess.run(
                ["make", "lint", "RUFF=false", "CPP_FORMAT_FILES=",
                 f"PYTHON={sys.executable}"],
                cwd=ROOT, env={**os.environ, "PATH": directory + os.pathsep + os.environ["PATH"]},
                capture_output=True, text=True, check=False, timeout=30,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Ruff is required", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
