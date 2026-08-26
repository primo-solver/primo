; UNSAT by counting: r != f(p) forces r to be f(p)'s complement, f(p) != f(q)
; forces f(q) = r, and then f(r) = f(f(p)) makes f both constant and
; non-constant. Two-valuedness is used at every step.
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun r () Bool)
(declare-fun f (Bool) Bool)
(assert (distinct r (f p)))
(assert (distinct (f p) (f q)))
(assert (= (f r) (f (f p))))
(check-sat)
