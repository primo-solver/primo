; UNSAT by ordinary forward congruence: p = q implies f(p) = f(q). Primo
; already gets this right in pure QF_UF -- kept so a future fix is measured
; against a shape that must not regress, not only against shapes that fail.
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(assert (= p q))
(assert (distinct (f p) (f q)))
(check-sat)
