; expected: sat
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun a () A)
(declare-fun b () A)
(assert (not (= a b)))
(check-sat)
