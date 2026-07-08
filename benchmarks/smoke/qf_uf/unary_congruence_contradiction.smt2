; expected: unsat
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun f (A) A)
(declare-fun a () A)
(declare-fun b () A)
(assert (= a b))
(assert (not (= (f a) (f b))))
(check-sat)
