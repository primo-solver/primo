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

SCRIPT = Path(__file__).parents[2] / "scripts" / "tactic_composition_matrix.py"
SPEC = importlib.util.spec_from_file_location("tactic_composition_matrix", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class TacticCompositionMatrixTests(unittest.TestCase):
    def write_executable(self, path: Path, body: str) -> None:
        path.write_text("#!/usr/bin/env python3\n" + body, encoding="utf-8")
        path.chmod(0o755)

    def write_case(self, path: Path) -> None:
        path.write_text(
            """(set-logic QF_UF)
(declare-fun p () Bool)
(assert p)
(check-sat)
""",
            encoding="utf-8",
        )

    def write_manifest(self, path: Path, case: Path) -> None:
        path.write_text(
            json.dumps(
                {
                    "profiles": [
                        {
                            "id": "profile",
                            "args": ["--preprocessing-mode=off"],
                            "logics": ["QF_UF"],
                            "required_stats": [":tactic.required.invocations"],
                        }
                    ],
                    "cases": [{"path": str(case), "logic": "QF_UF", "expected": "sat"}],
                }
            )
            + "\n",
            encoding="utf-8",
        )

    def test_manifest_rejects_duplicate_profile_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "matrix.json"
            profile = {"id": "same", "args": [], "logics": ["QF_UF"]}
            manifest.write_text(
                json.dumps({"profiles": [profile, profile], "cases": []}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "duplicate profile"):
                MODULE.load_manifest(manifest, root)

    def test_classification_requires_real_tactic_counter(self) -> None:
        result = MODULE.CommandResult("done", 0, "sat\n", "", 0.01)
        self.assertEqual(
            MODULE.classify(result, "sat", "sat", ("sat",), {}, (":required",)),
            "missing-statistic",
        )
        self.assertEqual(
            MODULE.classify(
                result, "sat", "sat", ("sat",), {":required": 1}, (":required",)
            ),
            "pass",
        )

    def test_reference_crash_is_not_treated_as_inconclusive(self) -> None:
        case = MODULE.Case(Path("case.smt2"), "QF_UF", None)
        with mock.patch.object(
            MODULE,
            "run_command",
            return_value=MODULE.CommandResult("done", 1, "", "failure", 0.01),
        ):
            results, failures = MODULE.reference_answers([case], [["reference"]], 1.0)
        self.assertEqual(results[case.path], ("crash",))
        self.assertEqual(len(failures), 1)

    def test_public_runner_repeats_and_records_deterministic_statistics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            manifest = root / "matrix.json"
            solver = root / "solver.py"
            reference = root / "reference.py"
            output = root / "out"
            self.write_case(case)
            self.write_manifest(manifest, case)
            self.write_executable(
                solver,
                "import sys\n"
                "assert '--preprocessing-mode=off' in sys.argv\n"
                "print('sat')\n"
                "print(':tactic.required.invocations 1', file=sys.stderr)\n",
            )
            self.write_executable(reference, "print('sat')\n")

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--manifest",
                    str(manifest),
                    "--root",
                    str(root),
                    "--solver",
                    str(solver),
                    "--refs",
                    str(reference),
                    "--repeat",
                    "2",
                    "--skip-model-validation",
                    "--out",
                    str(output),
                ],
                check=False,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            with (output / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual([row["outcome"] for row in rows], ["pass", "pass"])
            metadata = json.loads(
                (output / "metadata.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata["status"], "passed")
            self.assertEqual(metadata["rows"], 2)

    def test_public_runner_rejects_nondeterministic_statistics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / "case.smt2"
            manifest = root / "matrix.json"
            solver = root / "solver.py"
            reference = root / "reference.py"
            counter = root / "counter"
            output = root / "out"
            self.write_case(case)
            self.write_manifest(manifest, case)
            self.write_executable(
                solver,
                "import pathlib, sys\n"
                f"counter = pathlib.Path({str(counter)!r})\n"
                "value = int(counter.read_text()) + 1 if counter.exists() else 1\n"
                "counter.write_text(str(value))\n"
                "print('sat')\n"
                "print(':tactic.required.invocations 1', file=sys.stderr)\n"
                "print(f':changing {value}', file=sys.stderr)\n",
            )
            self.write_executable(reference, "print('sat')\n")

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--manifest",
                    str(manifest),
                    "--solver",
                    str(solver),
                    "--refs",
                    str(reference),
                    "--repeat",
                    "2",
                    "--skip-model-validation",
                    "--out",
                    str(output),
                ],
                check=False,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 1)
            with (output / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(rows[1]["outcome"], "invalid-statistics")


if __name__ == "__main__":
    unittest.main()
