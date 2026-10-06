"""Exercise GDB sampling exit handling and timeout cleanup on tiny programs."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SAMPLER = Path(os.environ.get(
    "PRIMO_PROFILE_SAMPLER",
    str(Path(__file__).resolve().parents[2] / "scripts/profile_gdb_samples.py"),
))


@unittest.skipUnless(shutil.which("gdb"), "GDB is required for profiler integration checks")
class GdbSamplesTests(unittest.TestCase):
    def run_profile(self, command: list[str], timeout: float) -> tuple[int, dict, str]:
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "profile"
            result = subprocess.run(
                [sys.executable, str(SAMPLER), "--out", str(out), "--timeout", str(timeout),
                 "--interval", "0.05", "--", *command],
                capture_output=True, text=True, timeout=20, check=False,
            )
            report = json.loads((out / "summary.json").read_text())
            log = (out / "gdb-mi.log").read_text()
            samples = [json.loads(line) for line in (out / "samples.jsonl").read_text().splitlines()]
            self.assertEqual(len(samples), report["samples"])
            if report["samples"]:
                self.assertTrue(all(row["frames"] for row in samples))
            return result.returncode, report, log

    def test_normal_exit_has_samples(self) -> None:
        code, report, _ = self.run_profile(["/usr/bin/sleep", "0.5"], 10)
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "complete")
        self.assertGreater(report["samples"], 0)

    def test_answer_followed_by_error_is_not_success(self) -> None:
        code, report, log = self.run_profile(["/usr/bin/sh", "-c", "printf 'sat\\n'; exit 7"], 10)
        self.assertNotEqual(code, 0)
        self.assertEqual(report["status"], "failed")
        self.assertIn("sat", log)
        self.assertIn('exit-code="07"', report["exit_reason"])

    def test_deadline_kills_inferior(self) -> None:
        code, report, log = self.run_profile(["/usr/bin/sleep", "10"], 0.4)
        self.assertNotEqual(code, 0)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["diagnostic"], "profile deadline exceeded")
        pids = re.findall(r'=thread-group-started,.*pid="(\d+)"', log)
        self.assertEqual(len(pids), 1)
        stat = Path(f"/proc/{pids[0]}/stat")
        # A killed orphan may briefly await reaping; it must not be executing.
        if stat.exists():
            self.assertEqual(stat.read_text().split(") ", 1)[1].split()[0], "Z")


if __name__ == "__main__":
    unittest.main()
