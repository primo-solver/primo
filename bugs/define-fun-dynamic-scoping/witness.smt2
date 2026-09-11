(set-logic QF_LIA)
(declare-fun b () Int)
(define-fun f ((a Int)) Int (+ a b))
; Inside the let, the local b is 5, but f's body refers to the global b.
; Lexical scoping: (f 1) = 1 + b, so the assertion says b /= 5 -> sat.
; Dynamic scoping: (f 1) = 1 + 5 = 6, the assertion is (not (= 6 6)) -> unsat.
(assert (let ((b 5)) (not (= (f 1) (+ 1 b)))))
(check-sat)
