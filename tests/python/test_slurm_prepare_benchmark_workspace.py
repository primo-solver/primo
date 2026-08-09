#!/usr/bin/env python3

from __future__ import annotations

import contextlib
import importlib.util
import io
import subprocess
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).parents[2] / "scripts" / "slurm_prepare_benchmark_workspace.py"
SPEC = importlib.util.spec_from_file_location("slurm_prepare_benchmark_workspace", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SshDiagnosticsTest(unittest.TestCase):
    def test_failure_is_printed_before_it_is_raised(self) -> None:
        failed = subprocess.CompletedProcess(
            ["ssh"], returncode=2, stdout="remote stdout\n", stderr="remote stderr\n"
        )
        output = io.StringIO()

        with (
            mock.patch.object(MODULE, "shell_syntax_check"),
            mock.patch.object(MODULE, "run", return_value=failed) as run,
            contextlib.redirect_stdout(output),
            self.assertRaises(subprocess.CalledProcessError),
        ):
            MODULE.ssh(Path("config"), "cluster", "false")

        run.assert_called_once_with(
            ["ssh", "-F", "config", "cluster", "false"], check=False
        )
        self.assertEqual(output.getvalue(), "remote stdout\nremote stderr\n")


if __name__ == "__main__":
    unittest.main()
