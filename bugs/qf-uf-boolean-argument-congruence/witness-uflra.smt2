; The same formula in QF_UFLRA with an unconstrained Real declaration. Also
; sat (wrong): with no Real ATOM the input is routed to the same pure QF_UF
; path. Adding any Real atom, e.g. (assert (< 0 x)), moves it to the mixed
; path, which DOES bridge, and Primo then answers unsat.
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
