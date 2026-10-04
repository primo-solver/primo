from __future__ import annotations

import argparse
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
    def test_unsat_only_run_does_not_require_dolmen(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case, manifest, solver = root / "case.smt2", root / "matrix.json", root / "solver.py"
            self.write_case(case)
            self.write_manifest(manifest, case)
            data = json.loads(manifest.read_text())
            data["cases"][0]["expected"] = "unsat"
            manifest.write_text(json.dumps(data))
            self.write_executable(solver, "import sys\nprint('unsat')\n"
                                  "print(':tactic.required.invocations 1', file=sys.stderr)\n")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--manifest", str(manifest), "--solver", str(solver),
                 "--refs", str(solver), "--dolmen", str(root / "missing-dolmen"),
                 "--repeat", "1", "--out", str(root / "out")],
                check=False, text=True, capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            coverage = json.loads((root / "out/profile/model-validation/coverage.json").read_text())
            self.assertEqual(coverage["eligible_models"], 0)

    def test_model_selection_and_exact_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile = MODULE.Profile("p", (), frozenset(), (), {}, {})
            args = argparse.Namespace(out=root, dolmen="dolmen", timeout=1,
                                      dolmen_timeout=1, allow_inconclusive=True)
            def measurement(file, answer="sat", outcome="pass", repeat="0"):
                return {"profile": "p", "file": str(root / file), "answer": answer,
                        "outcome": outcome, "repeat": repeat}
            rows = [measurement("sat.smt2"), measurement("sat.smt2", repeat="1"),
                    measurement("unsat.smt2", "unsat"), measurement("bad.smt2", outcome="crash")]
            output = root / "p/model-validation"
            for outcomes, rc, fail, incomplete in [
                ([("sat.smt2", "pass")], 0, False, 0),
                ([("sat.smt2", "not-sat")], 1, True, 0),
                ([("sat.smt2", "answer-changed")], 1, True, 0),
                ([("sat.smt2", "solver-timeout")], 1, False, 1),
                ([("sat.smt2", "solver-unknown")], 1, False, 1),
                ([], 0, True, 0),
                ([("other.smt2", "pass")], 0, True, 0),
                ([("sat.smt2", "pass"), ("sat.smt2", "pass")], 0, True, 0),
                ([("sat.smt2", "pass")], 2, True, 0),
            ]:
                def validate(command, **kwargs):
                    self.assertIn("--expect-sat", command)
                    self.assertEqual((output / "cases.txt").read_text().splitlines(),
                                     [str(root / "sat.smt2")])
                    with (output / "summary.csv").open("w") as handle:
                        writer = csv.writer(handle)
                        writer.writerow(["file", "outcome"])
                        writer.writerows((str(root / path), outcome) for path, outcome in outcomes)
                    return subprocess.CompletedProcess(command, rc)
                with self.subTest(outcomes=outcomes, rc=rc), mock.patch.object(
                    MODULE.subprocess, "run", side_effect=validate
                ):
                    failures, pending = MODULE.run_model_validation(profile, rows, args, "primo")
                    self.assertEqual(bool(failures), fail)
                    self.assertEqual(pending, incomplete)
                    coverage = json.loads((output / "coverage.json").read_text())
                    self.assertEqual(coverage["eligible_models"], 1)
                    self.assertEqual(coverage["measured_sat_rows"], 2)
                    self.assertEqual(len(coverage["rows"]), 2)
            with mock.patch.object(MODULE.subprocess, "run") as run:
                self.assertEqual(MODULE.run_model_validation(profile, rows[2:], args, "primo"), (0, 0))
                run.assert_not_called()
                self.assertEqual(json.loads((output / "coverage.json").read_text())["eligible_models"], 0)

    def test_qf_uflia_is_a_supported_matrix_logic(self) -> None:
        self.assertIn("QF_UFLIA", MODULE.LOGICS)

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

    def test_solver_revision_comes_from_binary_version(self) -> None:
        version = """primo 0.1.0
git: 4391dfc
build: Release, sanitizers: none
"""
        self.assertEqual(MODULE.solver_revision(version), "4391dfc")
        self.assertEqual(MODULE.solver_revision("primo 0.1.0\n"), "unknown")

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
            self.assertEqual(metadata["commit"], "unknown")
            self.assertNotEqual(metadata["workspace_commit"], "")

    def test_later_leaf_counter_may_be_observed_on_only_one_case(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            early = root / "early.smt2"
            later = root / "later.smt2"
            manifest = root / "matrix.json"
            solver = root / "solver.py"
            reference = root / "reference.py"
            output = root / "out"
            self.write_case(early)
            self.write_case(later)
            manifest.write_text(
                json.dumps(
                    {
                        "profiles": [
                            {
                                "id": "profile",
                                "args": [],
                                "logics": ["QF_UF"],
                                "required_observed_stats_by_logic": {
                                    "QF_UF": [":tactic.later.invocations"]
                                },
                            }
                        ],
                        "cases": [
                            {"path": str(early), "logic": "QF_UF", "expected": "sat"},
                            {"path": str(later), "logic": "QF_UF", "expected": "sat"},
                        ],
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            self.write_executable(
                solver,
                "import pathlib, sys\n"
                "print('sat')\n"
                "if pathlib.Path(sys.argv[-1]).name == 'later.smt2':\n"
                "    print(':tactic.later.invocations 1', file=sys.stderr)\n",
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
                    "1",
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
            profile_check = json.loads(
                (output / "profile" / "profile-check.json").read_text(encoding="utf-8")
            )
            self.assertEqual(profile_check["missing_observations"], {})

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
