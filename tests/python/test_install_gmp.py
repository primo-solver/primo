#!/usr/bin/env python3
"""GMP download failures must not prevent trying another release mirror."""

from __future__ import annotations

import hashlib
import http.client
import importlib.util
import io
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).parents[2] / "contrib" / "install_gmp.py"
SPEC = importlib.util.spec_from_file_location("install_gmp", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InstallGmpTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.destination = Path(temporary.name) / "gmp.tar.xz"
        patch = mock.patch.dict(
            MODULE.RELEASE_SHA256, {"6.3.0": hashlib.sha256(b"release").hexdigest()}
        )
        patch.start()
        self.addCleanup(patch.stop)

    def test_unreachable_primary_uses_mirror(self) -> None:
        with mock.patch.object(
            MODULE.urllib.request,
            "urlopen",
            side_effect=[
                urllib.error.URLError(OSError(101, "Network is unreachable")),
                io.BytesIO(b"release"),
            ],
        ) as download:
            MODULE.download_archive("6.3.0", None, self.destination)
        self.assertEqual(self.destination.read_bytes(), b"release")
        self.assertEqual(
            [call.args[0] for call in download.call_args_list],
            [
                MODULE.URL_TEMPLATE.format(version="6.3.0"),
                MODULE.MIRROR_TEMPLATE.format(version="6.3.0"),
            ],
        )

    def test_bad_digest_uses_mirror(self) -> None:
        with mock.patch.object(
            MODULE.urllib.request,
            "urlopen",
            side_effect=[
                io.BytesIO(b"corrupt"),
                io.BytesIO(b"release"),
            ],
        ):
            MODULE.download_archive("6.3.0", None, self.destination)
        self.assertEqual(self.destination.read_bytes(), b"release")

    def test_interrupted_read_uses_mirror(self) -> None:
        response = mock.MagicMock()
        response.__enter__.return_value.read.side_effect = http.client.IncompleteRead(
            b"part"
        )
        with mock.patch.object(
            MODULE.urllib.request,
            "urlopen",
            side_effect=[
                response,
                io.BytesIO(b"release"),
            ],
        ):
            MODULE.download_archive("6.3.0", None, self.destination)
        self.assertEqual(self.destination.read_bytes(), b"release")

    def test_all_sources_fail_without_writing_archive(self) -> None:
        with mock.patch.object(
            MODULE.urllib.request, "urlopen", side_effect=TimeoutError
        ):
            with self.assertRaisesRegex(
                SystemExit, "no build was attempted"
            ) as failure:
                MODULE.download_archive("6.3.0", None, self.destination)
        self.assertIn("ftp.gnu.org", str(failure.exception))
        self.assertIn("mirrors.kernel.org", str(failure.exception))
        self.assertFalse(self.destination.exists())

    def test_explicit_url_does_not_fall_back(self) -> None:
        with mock.patch.object(
            MODULE.urllib.request, "urlopen", side_effect=OSError
        ) as download:
            with self.assertRaises(SystemExit):
                MODULE.download_archive(
                    "6.3.0", "https://example.invalid/gmp", self.destination
                )
        download.assert_called_once_with("https://example.invalid/gmp", timeout=30)

    def test_primary_success_needs_no_mirror(self) -> None:
        with mock.patch.object(
            MODULE.urllib.request, "urlopen", return_value=io.BytesIO(b"release")
        ) as download:
            MODULE.download_archive("6.3.0", None, self.destination)
        self.assertEqual(download.call_count, 1)
        self.assertEqual(self.destination.read_bytes(), b"release")


if __name__ == "__main__":
    unittest.main()
