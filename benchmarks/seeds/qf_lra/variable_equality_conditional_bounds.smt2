; Equality propagation must retain the premises fixing x, not identify it with
; y merely because both initially have value zero. The p branch conflicts;
; the other branch has a model with x > y = 0.
(set-logic QF_LRA)
(set-info :status sat)
(declare-fun p () Bool)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (>= x 0))
(assert (>= y 0))
(assert (<= y 0))
(assert (or (not p) (<= x 0)))
(assert (or p (> x y)))
(assert (or (not p) (< x y)))
(check-sat)
