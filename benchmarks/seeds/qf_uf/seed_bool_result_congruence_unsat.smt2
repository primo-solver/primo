; UNSAT through a Bool-VALUED application used as a formula: p = q makes
; (g p q) and (g q q) congruent, so asserting one and denying the other is a
; contradiction. Also already correct in pure QF_UF.
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun g (Bool Bool) Bool)
(assert (= p q))
(assert (g p q))
(assert (not (g q q)))
(check-sat)
