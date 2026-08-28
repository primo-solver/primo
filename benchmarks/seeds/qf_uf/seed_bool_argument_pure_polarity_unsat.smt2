; UNSAT. tb collapses both elements of Bool, so tb(p) cannot differ from
; tb(true) whatever p is.
;
; The point of this seed is `(or p q)`: it makes p occur at a SINGLE
; propositional polarity while p is also a UF argument. Polarity is collected
; from formula structure only and never descends into arguments, so p looks
; "pure" to the pure-theory filter, which is then entitled to drop the very
; assignment congruence needs. The first version of the 2026-08-26 fix answered
; this sat, and the QF_UFLRA form of it was a REGRESSION -- main answered that
; unsat before the fix. Found by code review, not by the fuzz harness, which
; could not generate a bare propositional literal at the time.
(set-logic QF_UF)
(declare-sort U 0)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun tb (Bool) U)
(assert (= (tb true) (tb false)))
(assert (not (= (tb p) (tb true))))
(assert (or p q))
(check-sat)
