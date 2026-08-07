; Bool-sorted arguments to uninterpreted functions. SMT-LIB Bool is a
; first-class sort, so this is legal QF_UF, but no file in the SMT-LIB QF_UF
; division uses the shape -- which is why mutation fuzzing had no way to
; produce it and a congruence bug survived (docs/bool-argument-congruence-bug.md).
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun r () Bool)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(assert (= p q))
(assert (or (f p) r))
(assert (or (not (f q)) (not r)))
(assert (g p q))
(assert (or (not (g q p)) r))
(check-sat)
