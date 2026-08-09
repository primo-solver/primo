import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "slurm_stage_manifest_files.py"
SPEC = importlib.util.spec_from_file_location("slurm_stage_manifest_files", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
slurm_stage = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = slurm_stage
SPEC.loader.exec_module(slurm_stage)


class SlurmStageManifestFilesTests(unittest.TestCase):
    def test_all_smt2_paths_are_relative_sorted_and_filtered(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "z").mkdir()
            (root / "a").mkdir()
            (root / "z" / "second.smt2").write_text("(check-sat)\n", encoding="utf-8")
            (root / "a" / "first.smt2").write_text("(check-sat)\n", encoding="utf-8")
            (root / "ignored.txt").write_text("ignored\n", encoding="utf-8")

            self.assertEqual(
                slurm_stage.all_smt2_paths(root),
                ["a/first.smt2", "z/second.smt2"],
            )

    def test_all_smt2_paths_rejects_empty_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(SystemExit, "no SMT-LIB files found"):
                slurm_stage.all_smt2_paths(Path(temporary))

    def test_manifest_rejects_parent_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "manifest.txt"
            manifest.write_text("../outside.smt2\n", encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "unsafe manifest path"):
                slurm_stage.manifest_paths(manifest)

    def test_staging_uses_compression_and_delayed_replacement(self) -> None:
        command = slurm_stage.compressed_tar_command(
            Path("tmp/QF_UFLRA"), ["family/input.smt2"]
        )
        self.assertEqual(
            command[:6], ["tar", "-C", "tmp/QF_UFLRA", "-czf", "-", "--"]
        )

        remote = slurm_stage.remote_extract_script("/remote/work/tmp/QF_UFLRA")
        self.assertIn('staging="$remote_root.staging.$$"', remote)
        self.assertIn('tar -xzf - -C "$staging"', remote)
        self.assertLess(remote.index('tar -xzf'), remote.index('rm -rf "$remote_root"'))
        slurm_stage.shell_syntax_check(remote)


if __name__ == "__main__":
    unittest.main()
