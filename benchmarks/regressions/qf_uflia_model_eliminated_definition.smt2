; Reduced from witness-residual-2026-09-07.smt2 with an invalid-model predicate.
; The eliminated constant must equal the sum even if f(0) has no tableau value.
(set-option :produce-models true)
(set-logic QF_UFLIA)
(declare-fun z () Int)
(declare-fun f (Int) Int)
(assert (= z (+ (f 0) (f 0))))
(check-sat)
(get-model)
