; Minimized witness for an OpenSMT disagreement.
; Primo, Z3, cvc5, and Yices report unsat; OpenSMT 2.9.2 reported sat.
(set-logic QF_UFLRA)
(declare-fun a () Bool)
(declare-fun b () Bool)
(declare-fun f (Bool) Bool)
(declare-fun x () Real)
(assert (or (not a) b))
(assert (or (not b) a))
(assert (not (= (f a) (f b))))
(assert (<= 0 x))
(check-sat)
