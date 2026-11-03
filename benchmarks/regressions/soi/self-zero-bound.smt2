; Both coordinate walks have a zero minimum at the initial assignment.
; A real degenerate pivot enables progress; an own-bound zero update does not.
(set-logic QF_LRA)
(set-info :status sat)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (>= x 0))
(assert (>= y 0))
(assert (>= (+ x y) 1))
(assert (<= (- x (* 2 y)) 0))
(assert (<= (- y (* 2 x)) 0))
(check-sat)
