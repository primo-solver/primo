#!/usr/bin/env python3

from __future__ import annotations

import csv
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).parents[2] / "scripts" / "validate_qfuf_models_with_dolmen.py"
SPEC = importlib.util.spec_from_file_location("validate_qfuf_models_with_dolmen", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def solver_model() -> tuple[str, int, str, str, float]:
    return "done", 0, "sat\n(model\n  (define-fun x () Real 0)\n)\n", "", 0.01


class ModelValidationTests(unittest.TestCase):
    def write_case(self, path: Path) -> None:
        path.write_text(
            """(set-logic QF_UFLRA)
(declare-fun x () Real)
(declare-fun p (Real) Bool)
(assert (and (= x 0) (p x)))
(check-sat)
""",
            encoding="utf-8",
        )

    def run_main(
        self, arguments: list[str], side_effect: list[object]
    ) -> tuple[int, Path]:
        out = Path(arguments[arguments.index("--out") + 1])
        argv = [str(SCRIPT), *arguments]
        with (
            mock.patch.object(sys, "argv", argv),
            mock.patch.object(MODULE.shutil, "which", side_effect=lambda name: f"/tools/{name}"),
            mock.patch.object(MODULE, "run_command", side_effect=side_effect),
        ):
            result = MODULE.main()
        return result, out

    def test_completed_rows_survive_interruption(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "first.smt2"
            second = root / "second.smt2"
            self.write_case(first)
            self.write_case(second)
            out = root / "out"
            argv = [str(SCRIPT), "--out", str(out), str(first), str(second)]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    MODULE.shutil, "which", side_effect=lambda name: f"/tools/{name}"
                ),
                mock.patch.object(
                    MODULE,
                    "run_command",
                    side_effect=[solver_model(), ("done", 0, "", "", 0.01), RuntimeError("stop")],
                ),
                self.assertRaisesRegex(RuntimeError, "stop"),
            ):
                MODULE.main()

            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["outcome"], "pass")
            self.assertEqual(rows[0]["validator"], "dolmen")
            metadata = json.loads((out / "metadata.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["status"], "running")
            self.assertEqual(metadata["count"], 2)

    def test_records_z3_fallback_without_overwriting_dolmen_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            result, out = self.run_main(
                ["--out", str(root / "out"), str(case)],
                [
                    solver_model(),
                    ("done", 1, "", "dolmen rejected response", 0.01),
                    ("done", 0, "sat\n", "", 0.01),
                ],
            )
            self.assertEqual(result, 0)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                row = next(csv.DictReader(handle))
            self.assertEqual(row["validator"], "z3-fallback")
            self.assertEqual(row["dolmen_exit"], "1")
            self.assertEqual(row["z3_exit"], "0")

    def test_z3_only_path_records_empty_dolmen_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            out = root / "out"
            argv = [str(SCRIPT), "--out", str(out), str(case)]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    MODULE.shutil,
                    "which",
                    side_effect=lambda name: None if "dolmen" in name else f"/tools/{name}",
                ),
                mock.patch.object(
                    MODULE,
                    "run_command",
                    side_effect=[solver_model(), ("done", 0, "sat\n", "", 0.01)],
                ),
            ):
                result = MODULE.main()
            self.assertEqual(result, 0)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                row = next(csv.DictReader(handle))
            self.assertEqual(row["validator"], "z3-only")
            self.assertEqual(row["dolmen_exit"], "")
            self.assertEqual(row["z3_exit"], "0")

    def test_required_dolmen_pass_rejects_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            result, out = self.run_main(
                ["--require-dolmen-pass", "--out", str(root / "out"), str(case)],
                [
                    solver_model(),
                    ("done", 1, "", "dolmen rejected response", 0.01),
                    ("done", 0, "sat\n", "", 0.01),
                ],
            )
            self.assertEqual(result, 1)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                row = next(csv.DictReader(handle))
            self.assertEqual(row["outcome"], "fallback-pass")
            self.assertEqual(row["validator"], "z3-fallback")

    def test_unknown_is_not_counted_as_unsat(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            self.write_case(case)
            result, out = self.run_main(
                ["--out", str(root / "out"), str(case)],
                [("done", 0, "unknown\n", "", 0.01)],
            )
            self.assertEqual(result, 1)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                row = next(csv.DictReader(handle))
            self.assertEqual(row["outcome"], "solver-unknown")

    def test_manifest_root_and_sharding_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cases = root / "cases"
            cases.mkdir()
            for index in range(4):
                self.write_case(cases / f"case-{index}.smt2")
            manifest = root / "manifest.txt"
            manifest.write_text(
                "\n".join(f"cases/case-{index}.smt2" for index in range(4)) + "\n",
                encoding="utf-8",
            )
            result, out = self.run_main(
                [
                    "--manifest",
                    str(manifest),
                    "--root",
                    str(root),
                    "--shards",
                    "2",
                    "--shard-index",
                    "1",
                    "--out",
                    str(root / "out"),
                ],
                [solver_model(), ("done", 0, "", "", 0.01)] * 2,
            )
            self.assertEqual(result, 0)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(
                [Path(row["file"]).name for row in rows], ["case-1.smt2", "case-3.smt2"]
            )
            metadata = json.loads((out / "metadata.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["status"], "complete")
            self.assertEqual(metadata["completed"], 2)

    def test_fuzz_summarizer_reports_model_validation_separately(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            model = root / "task-0" / "model-validation" / "general"
            model.mkdir(parents=True)
            (model / "summary.csv").write_text(
                "file,answer,outcome,validator\ncase.smt2,sat,pass,dolmen\n",
                encoding="utf-8",
            )
            (model / "metadata.json").write_text(
                json.dumps({"status": "complete", "count": 1}), encoding="utf-8"
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).parents[2] / "scripts" / "summarize_fuzz_results.py"),
                    str(root),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("model-validation: summaries=1 cases=1", result.stdout)
            self.assertIn("validators={'dolmen': 1}", result.stdout)

    def test_fuzz_summarizer_recognizes_standalone_model_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shard = root / "shard-0"
            shard.mkdir()
            (shard / "summary.csv").write_text(
                "file,answer,outcome,validator,dolmen_exit\n"
                "sat.smt2,sat,pass,dolmen,0\n"
                "unsat.smt2,unsat,not-sat,,\n",
                encoding="utf-8",
            )
            (shard / "metadata.json").write_text(
                json.dumps(
                    {
                        "status": "complete",
                        "count": 2,
                        "require_dolmen_pass": True,
                    }
                ),
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).parents[2] / "scripts" / "summarize_fuzz_results.py"),
                    str(root),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("model-validation: summaries=1 cases=2", result.stdout)
            self.assertIn("{'pass': 1, 'not-sat': 1}", result.stdout)


if __name__ == "__main__":
    unittest.main()
