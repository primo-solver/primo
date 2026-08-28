; UNSAT by the CONVERSE of congruence: f is injective on the two-element Bool
; domain, so (= (f p) (f q)) forces p = q, and g then contradicts it. The
; argument equality is never asserted and is not derivable propositionally.
; Every QF_UFLRA bool-argument seed asserts the premise and checks the forward
; direction instead, which is why this shape was never covered.
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(assert (= (f p) (f q)))
(assert (distinct (f true) (f false)))
(assert (g p q))
(assert (not (g q q)))
(check-sat)
