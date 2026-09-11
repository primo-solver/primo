import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "generate_attribute_cases",
    Path(__file__).resolve().parents[2] / "scripts/generate_attribute_cases.py",
)
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class AttributeCasesTests(unittest.TestCase):
    def test_reproducible_and_unique(self):
        cases = GENERATOR.generate(200, 20260911)
        self.assertEqual(cases, GENERATOR.generate(200, 20260911))
        self.assertNotEqual(cases, GENERATOR.generate(200, 20260912))
        self.assertEqual(len({case["id"] for case in cases}), 200)

    def test_balanced_shapes_and_expectations(self):
        cases = GENERATOR.generate(200, 20260911)
        for metadata in (True, False):
            for valid in (True, False):
                selected = [
                    case
                    for case in cases
                    if case["input"].startswith("(set-info") == metadata
                    and case["valid"] == valid
                ]
                self.assertEqual(len(selected), 50)
                self.assertEqual(
                    {tuple(case["answers"]) for case in selected},
                    {("sat",), ("unsat",)},
                )

    def test_empty(self):
        self.assertEqual(GENERATOR.generate(0, 1), [])


if __name__ == "__main__":
    unittest.main()
