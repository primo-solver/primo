#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).parents[2] / "contrib" / "install_dolmen.py"
SPEC = importlib.util.spec_from_file_location("install_dolmen", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InstallDolmenTests(unittest.TestCase):
    def test_local_dependency_paths_preserve_existing_environment(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary) / "local"
            env = {
                "CFLAGS": "-O2",
                "LIBRARY_PATH": "/system/lib",
                "PKG_CONFIG_PATH": "/system/pkgconfig",
            }
            MODULE.add_local_dependency_paths(env, prefix)

            self.assertEqual(env["CFLAGS"], f"-I{prefix.resolve() / 'include'} -O2")
            self.assertEqual(env["CPPFLAGS"], f"-I{prefix.resolve() / 'include'}")
            self.assertEqual(env["LDFLAGS"], f"-L{prefix.resolve() / 'lib'}")
            self.assertEqual(
                env["LIBRARY_PATH"],
                f"{prefix.resolve() / 'lib'}{os.pathsep}/system/lib",
            )
            self.assertEqual(env["LD_LIBRARY_PATH"], f"{prefix.resolve() / 'lib'}")
            self.assertEqual(
                env["PKG_CONFIG_PATH"],
                f"{prefix.resolve() / 'lib/pkgconfig'}{os.pathsep}/system/pkgconfig",
            )

    def test_verified_cached_download_is_reused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "opam"
            destination.write_bytes(b"verified")
            digest = hashlib.sha256(b"verified").hexdigest()
            with mock.patch.object(MODULE.urllib.request, "urlretrieve") as retrieve:
                MODULE.download_verified("https://example.invalid/opam", digest, destination)
            retrieve.assert_not_called()

    def test_bad_cache_is_replaced_by_verified_download(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "opam"
            destination.write_bytes(b"stale")
            digest = hashlib.sha256(b"fresh").hexdigest()

            def retrieve(_url: str, path: Path) -> None:
                Path(path).write_bytes(b"fresh")

            with mock.patch.object(
                MODULE.urllib.request, "urlretrieve", side_effect=retrieve
            ):
                MODULE.download_verified("https://example.invalid/opam", digest, destination)
            self.assertEqual(destination.read_bytes(), b"fresh")

    def test_version_probe_requires_exact_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "dolmen"
            executable.write_text("#!/bin/sh\nprintf '0.10\\n'\n", encoding="utf-8")
            executable.chmod(0o755)
            self.assertTrue(MODULE.reports_version(executable, "0.10"))
            self.assertFalse(MODULE.reports_version(executable, "0.9"))


if __name__ == "__main__":
    unittest.main()
