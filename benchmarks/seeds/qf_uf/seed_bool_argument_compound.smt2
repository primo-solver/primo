; Compound Boolean arguments, which the frontend normalizes with a fresh
; Boolean constant plus a propositional equivalence (docs/architecture.md).
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(assert (f (and p q)))
(assert (or (not (f (and q p))) (f (or p q))))
(assert (or (f (not p)) (not (f (not q)))))
(check-sat)
