; Minimized witness for an OpenSMT disagreement.
; Primo, Z3, cvc5, and Yices report unsat; OpenSMT 2.9.2 reported sat.
(set-logic QF_UFLRA)
(declare-fun b1 () Bool)
(declare-fun b4 () Bool)
(declare-fun f (Bool) Bool)
(declare-fun x () Real)
(assert (or (not b1) b4))
(assert (or (not b4) b1))
(assert (not (= (f b1) (f b4))))
(assert (<= 0 x))
(check-sat)
