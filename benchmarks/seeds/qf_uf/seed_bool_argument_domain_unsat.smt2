; UNSAT, and the smallest shape Primo currently gets wrong (2026-08-26).
; tb is constant on Bool's two elements, so tb(p) = tb(q) for ANY p and q.
; Deciding that needs the Bool-sorted ARGUMENTS p and q to be known equal to
; one of the two constants. Note there is no Bool-valued function here at all:
; the missing link is about argument position only.
(set-logic QF_UF)
(declare-sort U 0)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun tb (Bool) U)
(assert (= (tb true) (tb false)))
(assert (not (= (tb p) (tb q))))
(check-sat)
