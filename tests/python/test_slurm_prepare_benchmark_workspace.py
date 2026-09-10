#!/usr/bin/env python3

from __future__ import annotations

import contextlib
import importlib.util
import io
import shlex
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).parents[2] / "scripts" / "slurm_prepare_benchmark_workspace.py"
SPEC = importlib.util.spec_from_file_location(
    "slurm_prepare_benchmark_workspace", SCRIPT
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SshDiagnosticsTest(unittest.TestCase):
    def test_extra_build_target_preserves_primary_version_check(self) -> None:
        command = MODULE.build_command(
            "/cluster/work",
            "cmake",
            "primo",
            2,
            "abc123",
            ["contrib-required"],
            True,
            ["primo_parser_corpus"],
        )
        MODULE.shell_syntax_check(command)
        self.assertIn("--target primo primo_parser_corpus -j2", command)
        self.assertIn("build-slurm-rel/primo --version", command)
        self.assertNotIn("build-slurm-rel/primo_parser_corpus --version", command)

    def test_reusable_dependencies_do_not_require_antlr(self) -> None:
        command = MODULE.build_command(
            "/cluster/work", "cmake", "primo", 2, "abc123", ["contrib-required"], True
        )
        MODULE.shell_syntax_check(command)
        self.assertNotIn("antlr", command.lower())
        self.assertIn("CLI/CLI.hpp", command)
        self.assertIn("libcadical.a", command)
        self.assertIn("libgmp.a", command)

    def test_portable_headers_are_validated_and_exported_before_configuration(
        self,
    ) -> None:
        command = MODULE.with_cxx_headers("cmake -B build", "/cluster/header roots.txt")
        MODULE.shell_syntax_check(command)
        self.assertIn("done < '/cluster/header roots.txt'", command)
        self.assertIn('test -d "$header_root"', command)
        self.assertLess(
            command.index("export CXXFLAGS="), command.index("cmake -B build")
        )

    def test_build_via_slurm_keeps_compilation_inside_allocation(self) -> None:
        with (
            mock.patch.object(
                sys,
                "argv",
                [
                    str(SCRIPT),
                    "--ssh-config",
                    str(SCRIPT),
                    "--remote-dir",
                    "/cluster/work space",
                    "--build-via-slurm",
                    "--jobs",
                    "2",
                    "--reuse-dependencies",
                ],
            ),
            mock.patch.object(
                MODULE,
                "run",
                side_effect=[
                    subprocess.CompletedProcess([], 0, "abc123\n", ""),
                    subprocess.CompletedProcess([], 0, "", ""),
                ],
            ),
            mock.patch.object(MODULE, "ensure_no_active_primo_jobs"),
            mock.patch.object(MODULE, "archive_repo"),
            mock.patch.object(MODULE, "ssh") as ssh,
        ):
            self.assertEqual(MODULE.main(), 0)
        command = next(
            call.args[2]
            for call in ssh.call_args_list
            if call.args[2].startswith("srun ")
        )
        MODULE.shell_syntax_check(command)
        words = shlex.split(command)
        self.assertIn("--cpus-per-task=2", words)
        self.assertEqual(words[-3:-1], ["bash", "-c"])
        self.assertIn("cd '/cluster/work space'", words[-1])
        self.assertIn("--target primo -j2", words[-1])
        self.assertIn("PRIMO_GIT_REVISION_OVERRIDE=abc123", words[-1])

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
