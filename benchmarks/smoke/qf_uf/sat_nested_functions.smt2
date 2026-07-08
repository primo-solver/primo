; expected: sat
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun f (A) A)
(declare-fun g (A) A)
(declare-fun a () A)
(declare-fun b () A)
(assert (= (f (g a)) b))
(check-sat)
