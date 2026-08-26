; Primo answers sat; z3 and cvc5 answer unsat.
;
; Unsatisfiable because (distinct (f false) (f true)) makes f injective on the
; two-element Bool domain, so (= (f p) (f q)) forces p = q, and then (g p q)
; is (g q q), contradicting (not (g q q)).
;
; The declared Real constant is never constrained. That is the point: the
; logic is QF_UFLRA but no Real ATOM survives to the solver, and Primo's
; Boolean-domain bridging -- the step that makes f's Bool argument congruent
; with the true/false constants -- does not run on that path.
(set-logic QF_UFLRA)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(declare-fun x () Real)
(assert (= (f p) (f q)))
(assert (not (g q q)))
(assert (distinct (f false) (f true)))
(assert (g p q))
(check-sat)
