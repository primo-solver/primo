; expected: unsat
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun a () A)
(declare-fun b () A)
(assert (= a b))
(assert (not (= a b)))
(check-sat)
