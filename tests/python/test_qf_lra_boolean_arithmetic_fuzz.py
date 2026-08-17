import ast
import csv
import importlib.util
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "qf_lra_boolean_arithmetic_fuzz.py"
SPEC = importlib.util.spec_from_file_location("qf_lra_boolean_arithmetic_fuzz", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
qf_lra_boolean_arithmetic_fuzz = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = qf_lra_boolean_arithmetic_fuzz
SPEC.loader.exec_module(qf_lra_boolean_arithmetic_fuzz)


class QfLraBooleanArithmeticFuzzTests(unittest.TestCase):
    def test_script_parses_with_cluster_python_grammar(self) -> None:
        source = SCRIPT.read_text(encoding="utf-8")
        ast.parse(source, filename=str(SCRIPT), feature_version=(3, 9))

    def make_fake_solver(self, directory: Path) -> Path:
        solver = directory / "solver"
        solver.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib\n"
            "import re\n"
            "import sys\n"
            "text = pathlib.Path(sys.argv[-1]).read_text(encoding='utf-8')\n"
            "print('sat')\n"
            "if '--stats' in sys.argv:\n"
            "    print(':preprocessing.monotone-elimination.candidates 1')\n"
            "    print(':preprocessing.monotone-elimination.atoms-folded 1')\n"
            "if '(get-model)' in text:\n"
            "    print('(model')\n"
            "    for m in re.finditer(r'\\(declare-fun (\\S+) \\(\\) (Real|Bool)\\)', text):\n"
            "        name, sort = m.group(1), m.group(2)\n"
            "        value = '0' if sort == 'Real' else 'true'\n"
            "        print(f'  (define-fun {name} () {sort} {value})')\n"
            "    print(')')\n",
            encoding="utf-8",
        )
        solver.chmod(0o755)
        return solver

    def make_fake_z3(self, directory: Path) -> Path:
        z3 = directory / "z3"
        z3.write_text("#!/usr/bin/env python3\nprint('sat')\n", encoding="utf-8")
        z3.chmod(0o755)
        return z3

    def make_fake_dolmen(self, directory: Path) -> Path:
        dolmen = directory / "dolmen"
        dolmen.write_text("#!/usr/bin/env python3\nimport sys\nsys.exit(0)\n", encoding="utf-8")
        dolmen.chmod(0o755)
        return dolmen

    def test_generate_case_covers_every_shape_in_order(self) -> None:
        rng = random.Random(11)
        shapes = [
            qf_lra_boolean_arithmetic_fuzz.generate_case(rng, index).shape
            for index in range(len(qf_lra_boolean_arithmetic_fuzz.SHAPES))
        ]
        self.assertEqual(tuple(shapes), qf_lra_boolean_arithmetic_fuzz.SHAPES)

    def test_generation_is_reproducible(self) -> None:
        first = qf_lra_boolean_arithmetic_fuzz.generate_case(random.Random(7), 20)
        second = qf_lra_boolean_arithmetic_fuzz.generate_case(random.Random(7), 20)
        self.assertEqual(first, second)

    def test_both_polarities_shares_one_literal_across_two_disjuncts(self) -> None:
        case = qf_lra_boolean_arithmetic_fuzz.build_both_polarities(random.Random(3))
        self.assertIn("(or (not (< ", case.text)
        self.assertIn(") p)", case.text)
        self.assertIn(") q)", case.text)

    def test_negate_equivalent_rejects_nested_operands(self) -> None:
        self.assertEqual(
            qf_lra_boolean_arithmetic_fuzz.negate_equivalent("(< v0 v1)"),
            "(not (<= v1 v0))",
        )
        with self.assertRaises(ValueError):
            qf_lra_boolean_arithmetic_fuzz.negate_equivalent("(< (+ v0 5) v1)")

    def test_chains_are_well_formed_smtlib(self) -> None:
        for index in range(3 * len(qf_lra_boolean_arithmetic_fuzz.SHAPES)):
            case = qf_lra_boolean_arithmetic_fuzz.generate_case(random.Random(99), index)
            self.assertIn("(set-logic QF_LRA)", case.text)
            self.assertIn("(check-sat)", case.text)
            self.assertEqual(case.text.count("("), case.text.count(")"))

    def test_cli_runs_end_to_end_against_fake_binaries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            solver = self.make_fake_solver(root)
            z3 = self.make_fake_z3(root)
            dolmen = self.make_fake_dolmen(root)
            out = root / "out"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--solver",
                    str(solver),
                    "--z3",
                    str(z3),
                    "--dolmen",
                    str(dolmen),
                    "--count",
                    "21",
                    "--seed",
                    "5",
                    "--out",
                    str(out),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with (out / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(len(rows), 21)
            self.assertEqual({row["outcome"] for row in rows}, {"PASS"})
            self.assertEqual(
                {row["shape"] for row in rows}, set(qf_lra_boolean_arithmetic_fuzz.SHAPES)
            )

    def test_cli_reports_dolmen_model_error_as_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            solver = self.make_fake_solver(root)
            z3 = self.make_fake_z3(root)
            broken_dolmen = root / "dolmen"
            broken_dolmen.write_text(
                "#!/usr/bin/env python3\nimport sys\nsys.exit(1)\n", encoding="utf-8"
            )
            broken_dolmen.chmod(0o755)
            out = root / "out"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--solver",
                    str(solver),
                    "--z3",
                    str(z3),
                    "--dolmen",
                    str(broken_dolmen),
                    "--count",
                    str(len(qf_lra_boolean_arithmetic_fuzz.SHAPES)),
                    "--seed",
                    "5",
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
            self.assertEqual({row["outcome"] for row in rows}, {"MODEL_ERROR"})
            self.assertEqual(
                len(list((out / "failures").glob("*.smt2"))),
                len(qf_lra_boolean_arithmetic_fuzz.SHAPES),
            )


if __name__ == "__main__":
    unittest.main()
