; Primo answers sat. z3 and cvc5 answer unsat, and Primo's own printed model
; does not satisfy the formula.
;
; Unsat because (distinct (f false) (f true)) makes f injective on the
; two-element Bool domain, so (= (f p) (f q)) forces p = q. Then (g p q) is
; (g q q), contradicting (not (g q q)).
;
; Deciding that needs p and q -- Bool-sorted ARGUMENTS of uninterpreted
; functions -- to be congruent with the constants true and false. The pure
; QF_UF path never establishes that link, so congruence closure is free to
; treat p as a third Boolean value.
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(assert (= (f p) (f q)))
(assert (not (g q q)))
(assert (distinct (f false) (f true)))
(assert (g p q))
(check-sat)
