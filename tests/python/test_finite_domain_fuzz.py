import ast
import csv
import importlib.util
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "finite_domain_fuzz.py"
SPEC = importlib.util.spec_from_file_location("finite_domain_fuzz", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
finite_domain_fuzz = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = finite_domain_fuzz
SPEC.loader.exec_module(finite_domain_fuzz)


class FiniteDomainFuzzTests(unittest.TestCase):
    def test_script_parses_with_cluster_python_grammar(self) -> None:
        source = SCRIPT.read_text(encoding="utf-8")
        ast.parse(source, filename=str(SCRIPT), feature_version=(3, 9))

    def make_solver(self, directory: Path, with_stats: bool = True) -> Path:
        solver = directory / (
            "solver-with-stats" if with_stats else "solver-without-stats"
        )
        stats = (
            """
if "--stats" in sys.argv:
    is_mixed = "QF_UFLRA" in text
    print(":preprocessing.finite-domain.raw-choices 1")
    print(":preprocessing.finite-domain.raw-amo-clauses 3")
    print(":preprocessing.finite-domain.post-amo-clauses 2")
    print(":preprocessing.finite-domain.amo-clauses 3")
    print(f":preprocessing.real-equality-lowering.lowered {6 if is_mixed else 0}")
    print(f":solver.qf-uf {0 if is_mixed else 1}")
    print(f":solver.qf-uflra {1 if is_mixed else 0}")
"""
            if with_stats
            else ""
        )
        solver.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib\n"
            "import sys\n"
            "text = pathlib.Path(sys.argv[-1]).read_text(encoding='utf-8')\n"
            + stats
            + "expected = next(line.split(':', 1)[1].strip() for line in text.splitlines() "
            "if line.startswith('; expected:'))\n"
            "print(expected)\n",
            encoding="utf-8",
        )
        solver.chmod(0o755)
        return solver

    def test_scenario_slots_cover_every_logic_scenario_and_verdict(self) -> None:
        logics = ("QF_UF", "QF_UFLRA")
        slot_count = len(logics) * len(finite_domain_fuzz.SCENARIOS) * 2
        slots = {
            finite_domain_fuzz.scenario_slot(index, logics)
            for index in range(slot_count)
        }
        expected = {
            (logic, scenario, verdict)
            for logic in logics
            for scenario in finite_domain_fuzz.SCENARIOS
            for verdict in ("sat", "unsat")
        }
        self.assertEqual(slots, expected)

    def test_generation_preflight_executes_every_slot(self) -> None:
        logics = ("QF_UF", "QF_UFLRA")
        cases = finite_domain_fuzz.preflight_generation(23, logics)
        self.assertEqual(len(cases), len(logics) * len(finite_domain_fuzz.SCENARIOS) * 2)
        self.assertEqual(
            {(case.logic, case.scenario, case.expected) for case in cases},
            {
                (logic, scenario, verdict)
                for logic in logics
                for scenario in finite_domain_fuzz.SCENARIOS
                for verdict in ("sat", "unsat")
            },
        )
        alias_cases = [case for case in cases if case.scenario == "define-fun-aliases"]
        self.assertTrue(alias_cases)
        self.assertTrue(all("(define-fun a0" in case.text for case in alias_cases))

    def test_generation_is_reproducible_and_mixed_cases_are_genuinely_mixed(
        self,
    ) -> None:
        first = finite_domain_fuzz.generate_case(
            random.Random(17), 15, ("QF_UF", "QF_UFLRA")
        )
        second = finite_domain_fuzz.generate_case(
            random.Random(17), 15, ("QF_UF", "QF_UFLRA")
        )
        self.assertEqual(first, second)
        self.assertEqual(first.logic, "QF_UFLRA")
        self.assertIn("(declare-fun p (Real) Bool)", first.text)
        self.assertIn("(< (+ (f x) 1) (+ (f x) 2))", first.text)

    def test_parse_stats_ignores_answers_and_non_integer_values(self) -> None:
        stats = finite_domain_fuzz.parse_stats("sat\n:a 3\n:b nope\n:c 0\n")
        self.assertEqual(stats, {":a": 3, ":c": 0})

    def test_cli_flushes_complete_rows_and_records_activation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            solver = self.make_solver(root)
            out = root / "out"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--solver",
                    str(solver),
                    "--ref",
                    str(solver),
                    "--logic",
                    "mixed",
                    "--count",
                    "28",
                    "--out",
                    str(out),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(len(rows), 28)
            self.assertEqual({row["outcome"] for row in rows}, {"PASS"})
            self.assertEqual(
                {row["scenario"] for row in rows}, set(finite_domain_fuzz.SCENARIOS)
            )

            gate = root / "gate"
            shutil.copytree(out, gate / "task-0" / "finite-qf-uf")
            general = gate / "task-0" / "general"
            general.mkdir()
            (general / "summary.csv").write_text(
                "case,outcome\ngeneral.smt2,PASS\n", encoding="utf-8"
            )
            summarized = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "summarize_fuzz_results.py"),
                    str(gate),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                summarized.returncode, 0, summarized.stdout + summarized.stderr
            )
            self.assertIn("finite-domain", summarized.stdout)

    def test_missing_activation_fails_and_preserves_the_case(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            solver = self.make_solver(root, with_stats=False)
            out = root / "out"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--solver",
                    str(solver),
                    "--ref",
                    str(solver),
                    "--logic",
                    "QF_UFLRA",
                    "--count",
                    "14",
                    "--out",
                    str(out),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual({row["outcome"] for row in rows}, {"ACTIVATION_MISSING"})
            self.assertEqual(len(list((out / "failures").glob("*.smt2"))), 14)


if __name__ == "__main__":
    unittest.main()
