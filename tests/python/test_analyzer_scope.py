from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "analyzer", Path(__file__).parents[2] / "scripts/analyze_smt2.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def term(text):
    return MODULE.parse(MODULE.tokenize(text))[0]


class AnalyzerScopeTests(unittest.TestCase):
    def test_parallel_rhs_uses_outer_scope(self):
        self.assertEqual(MODULE.inline_lets(term('(let ((x 1) (y x)) (+ x y))')),
                         term('(+ 1 x)'))
        self.assertEqual(MODULE.inline_lets(term('(let ((x 7)) (let ((x 1) (y x)) (+ x y)))')),
                         term('(+ 1 7)'))

    def test_shadowing_does_not_capture_substitution(self):
        self.assertEqual(MODULE.inline_lets(term('(let ((x (+ y 1))) (let ((y 2)) (+ x y)))')),
                         term('(+ (+ y 1) 2)'))
        self.assertEqual(MODULE.inline_lets(term('(+ (let ((x 1)) x) (let ((x 2)) x) x)')),
                         term('(+ 1 2 x)'))

    def test_nested_rhs_and_cycle_like_parallel_names(self):
        self.assertEqual(MODULE.inline_lets(term('(let ((x (let ((y 2)) y))) x)')), '2')
        self.assertEqual(MODULE.inline_lets(term('(let ((x y) (y x)) (+ x y))')), term('(+ y x)'))

    def test_rejects_malformed_and_unsupported_binders(self):
        for text in ('(let () x)', '(let ((x 1) (x 2)) x)', '(let ((x)) x)',
                     '(let (((x) 1)) x)', '(let ((1 x)) x)', '(let ((x 1)))',
                     '(forall ((x Real)) x)', '(exists ((x Real)) x)',
                     '(lambda ((x Real)) x)', '(match x ((y y)))'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                MODULE.inline_lets(term(text))

    def test_deep_expansion_is_iterative(self):
        node = 'x'
        for _ in range(3000):
            node = ['let', [['x', 'x']], node]
        self.assertEqual(MODULE.inline_lets(node), 'x')


if __name__ == '__main__':
    unittest.main()
