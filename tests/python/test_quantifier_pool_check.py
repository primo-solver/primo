import importlib.util
from pathlib import Path
import unittest


SOURCE = Path(__file__).resolve().parents[2] / 'scripts/quantifier_pool_check.py'
SPEC = importlib.util.spec_from_file_location('quantifier_pool_check', SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

EXPORT = """; Primo quantifier pool export (scripts/quantifier_pool_check.py)
(declare-fun $primo_skolem_0 () U)
(assert (or (not (exists ((?x0 U)) (p ?x0))) (p $primo_skolem_0)))
; primo-pool primo_pool_0 U
(declare-pool primo_pool_0 U (a $primo_skolem_0))
; primo-pool primo_pool_1 U U
(declare-pool primo_pool_1 (Tuple U U) ((tuple a a)))
"""


EXPORT_WITH_ROOTS = """; Primo quantifier pool export (scripts/quantifier_pool_check.py)
(declare-fun $primo_skolem_0 () U)
(declare-fun $primo_skolem_1 () U)
; primo-skolem-root exists 0 :sorts U :constants $primo_skolem_0 :lemma next
(assert (or (not (exists ((?x0 U)) (p ?x0))) (p $primo_skolem_0)))
; primo-skolem-root not-forall 0 :sorts U :constants $primo_skolem_1 :lemma none
; primo-pool primo_pool_0 U
(declare-pool primo_pool_0 U (a $primo_skolem_0 $primo_skolem_1))
"""

PRIMO_LEMMA = '(assert (or (not (exists ((?x0 U)) (p ?x0))) (p $primo_skolem_0)))'


class PoolCheckTests(unittest.TestCase):
    def test_top_level_skolemizations_use_the_input_text(self):
        source = ('(set-logic UF)\n(declare-sort U 0)\n(declare-fun a () U)\n'
                  '(declare-fun p (U) Bool)\n(assert (! (exists ((z U)) (p z)) :named goal))\n'
                  '(assert (let ((b a)) (not (forall ((y U)) (= y b)))))\n(check-sat)\n')
        text, counts = MODULE.build_check(source, EXPORT_WITH_ROOTS)
        lines = text.splitlines()
        self.assertNotIn(PRIMO_LEMMA, lines)
        self.assertIn('(assert (! (exists ((z U)) (! (p z) :pool (primo_pool_0))) :named goal))', lines)
        self.assertIn('(assert (let ((z $primo_skolem_0)) (p z)))', lines)
        self.assertIn('(assert (let ((b a)) (not (let ((y $primo_skolem_1)) (= y b)))))', lines)
        self.assertEqual(lines[-1], '(check-sat)')
        self.assertEqual((counts['skolem_linked'], counts['skolem_unlinked']), (2, 0))

    def test_unmatched_skolemizations_keep_primo_lemmas(self):
        source = ('(set-logic UF)\n(declare-sort U 0)\n(declare-fun a () U)\n'
                  '(declare-fun p (U) Bool)\n(assert (p a))\n'
                  '(assert (forall ((y U)) (p y)))\n(check-sat)\n')
        text, counts = MODULE.build_check(source, EXPORT_WITH_ROOTS)
        self.assertIn(PRIMO_LEMMA, text.splitlines())
        self.assertEqual((counts['skolem_linked'], counts['skolem_unlinked']), (0, 2))


    def test_tokens_drop_comments_and_keep_strings_and_quoted_symbols(self):
        tokens = MODULE.tokenize('(set-info :source "a ""b"" ; c") ; note\n(declare-fun |x y| () U)')
        self.assertIn('"a ""b"" ; c"', tokens)
        self.assertIn('|x y|', tokens)
        self.assertNotIn('note', tokens)

    def test_parse_and_serialize_round_trip(self):
        text = '(assert (forall ((x U)) (! (p x) :pattern ((p x)))))'
        self.assertEqual(MODULE.serialize(MODULE.parse(text)[0]), text)

    def test_annotation_wraps_bodies_extends_annotations_and_strips_back(self):
        pools = MODULE.read_pools(EXPORT)
        text = ('(assert (and (forall ((x |U|)) (p x)) '
                '(forall ((x U) (y U)) (! (r x y) :pattern ((r x y)))) (exists ((b Bool)) b)))')
        command = MODULE.parse(text)[0]
        annotated, added, missing = MODULE.annotate(command, pools)
        printed = MODULE.serialize(annotated)
        self.assertIn('(forall ((x |U|)) (! (p x) :pool (primo_pool_0)))', printed)
        self.assertIn('(! (r x y) :pattern ((r x y)) :pool (primo_pool_1))', printed)
        self.assertEqual((added, missing), (2, 1))
        stripped = MODULE.strip_pools(annotated, set(pools.values()))
        self.assertEqual(MODULE.serialize(stripped), text)
        self.assertEqual(MODULE.serialize(command), text)

    def test_nested_quantifiers_are_annotated(self):
        pools = MODULE.read_pools(EXPORT)
        command = MODULE.parse('(assert (forall ((x U)) (exists ((y U)) (p y))))')[0]
        annotated, added, _ = MODULE.annotate(command, pools)
        self.assertEqual(added, 2)
        self.assertEqual(
            MODULE.serialize(annotated),
            '(assert (forall ((x U)) (! (exists ((y U)) (! (p y) :pool (primo_pool_0))) '
            ':pool (primo_pool_0))))')

    def test_check_orders_options_declarations_export_and_assertions(self):
        source = ('(set-info :status unsat)\n(set-option :produce-models false)\n(set-logic UF)\n'
                  '(declare-sort U 0)\n(declare-fun a () U)\n(declare-fun p (U) Bool)\n'
                  '(assert (forall ((x U)) (p x)))\n(declare-fun b () U)\n'
                  '(assert (not (p b)))\n(check-sat)\n(get-model)\n(exit)\n')
        text, counts = MODULE.build_check(source, EXPORT)
        lines = text.splitlines()
        self.assertEqual(lines[0], '(set-option :produce-models false)')
        self.assertEqual(lines[1], '(set-logic ALL)')
        self.assertLess(lines.index('(declare-fun b () U)'),
                        lines.index('(declare-pool primo_pool_0 U (a $primo_skolem_0))'))
        self.assertLess(lines.index('(declare-pool primo_pool_1 (Tuple U U) ((tuple a a)))'),
                        lines.index('(assert (not (p b)))'))
        self.assertEqual(lines[-1], '(check-sat)')
        self.assertNotIn('(get-model)', lines)
        self.assertEqual((counts['assertions'], counts['annotated'], counts['pools']), (2, 1, 2))

    def test_definitions_follow_the_pools_they_use(self):
        source = ('(set-logic UF)\n(declare-sort U 0)\n(declare-fun p (U) Bool)\n'
                  '(define-fun all_p () Bool (forall ((x U)) (p x)))\n'
                  '(declare-fun a () U)\n(assert all_p)\n(assert (not (p a)))\n(check-sat)\n')
        text, counts = MODULE.build_check(source, EXPORT)
        lines = text.splitlines()
        definition = '(define-fun all_p () Bool (forall ((x U)) (! (p x) :pool (primo_pool_0))))'
        self.assertIn(definition, lines)
        self.assertLess(lines.index('(declare-fun a () U)'),
                        lines.index('(declare-pool primo_pool_0 U (a $primo_skolem_0))'))
        self.assertLess(lines.index('(declare-pool primo_pool_0 U (a $primo_skolem_0))'),
                        lines.index(definition))
        self.assertLess(lines.index(definition), lines.index('(assert all_p)'))
        self.assertEqual(counts['annotated'], 1)

    def test_verdicts(self):
        self.assertEqual(MODULE.verdict('unsat', 'unsat'), 'confirmed')
        self.assertEqual(MODULE.verdict('unsat', 'sat'), 'refuted')
        self.assertEqual(MODULE.verdict('unsat', 'timeout'), 'inconclusive')
        self.assertEqual(MODULE.verdict('unknown', 'sat'), 'control')
        self.assertEqual(MODULE.verdict('unknown', 'timeout'), 'control')
        self.assertEqual(MODULE.verdict('unknown', 'unsat'), 'control-unsat')

    def test_incremental_inputs_are_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.build_check('(set-logic UF)(push 1)(check-sat)', EXPORT)


if __name__ == '__main__':
    unittest.main()
