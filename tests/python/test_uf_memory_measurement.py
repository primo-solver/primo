"""Resource measurement must preserve failed exits and enforce child-only limits."""

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class UfMemoryMeasurementTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        text = (ROOT / "scripts/qualification/uf_memory/paired.sbatch").read_text()
        python = text.split("python3 - <<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
        module_path = self.root / "measurement.py"
        module_path.write_text(python)
        spec = importlib.util.spec_from_file_location(
            "uf_memory_measurement", module_path
        )
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)

    def run_child(self, body, *, timeout=2):
        return self.module.measure(
            [sys.executable, "-c", body],
            self.root / "run",
            timeout,
            256 * 1024**2,
            interval=0.01,
        )

    def test_answer_followed_by_failed_exit_is_not_solved(self):
        row = self.run_child("print('sat', flush=True); raise SystemExit(3)")
        self.assertEqual(row["answer"], "sat")
        self.assertEqual(row["exit_code"], 3)
        self.assertEqual(row["status"], "error")
        self.assertGreater(row["max_rss_kib"], 0)

    def test_timeout_overrides_cooperative_unknown_and_cleans_descendants(self):
        pid_file = self.root / "child.pid"
        body = (
            "import signal,subprocess,sys,time; from pathlib import Path; "
            "signal.signal(signal.SIGTERM,lambda *args:(print('unknown',flush=True),sys.exit(0))); "
            "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)']); "
            f"Path({str(pid_file)!r}).write_text(str(child.pid)); time.sleep(60)"
        )
        row = self.run_child(body, timeout=0.2)
        self.assertEqual(row["status"], "timeout")
        self.assertTrue(row["timed_out"])
        if pid_file.exists():
            pid = int(pid_file.read_text())
            status = Path(f"/proc/{pid}/status")
            if status.exists():
                self.assertRegex(status.read_text(), r"State:\s+Z")

    def test_observed_limit_and_native_statistics_are_recorded(self):
        body = (
            "import resource,sys,time; "
            "assert resource.getrlimit(resource.RLIMIT_AS)==(268435456,268435456); "
            "print('unsat'); print(':euf.incremental.terms 123',file=sys.stderr); time.sleep(.1)"
        )
        row = self.run_child(body)
        self.assertEqual(row["status"], "solved")
        self.assertIn("268435456", row["observed_limits"])
        self.assertEqual(row["statistics"][":euf.incremental.terms"], 123)

    def test_resource_failure_is_distinct_from_cleanup_abort(self):
        status, _ = self.module.classify("", "std::bad_alloc", 2, False)
        self.assertEqual(status, "explicit-allocation-failure")
        status, _ = self.module.classify(
            "", "disconnect_external_propagator: solver in invalid state", -6, False
        )
        self.assertEqual(status, "cleanup-abort")

    def test_parent_address_space_limit_is_unchanged(self):
        import resource

        before = resource.getrlimit(resource.RLIMIT_AS)
        self.run_child("print('unsat')")
        self.assertEqual(resource.getrlimit(resource.RLIMIT_AS), before)


if __name__ == "__main__":
    unittest.main()
