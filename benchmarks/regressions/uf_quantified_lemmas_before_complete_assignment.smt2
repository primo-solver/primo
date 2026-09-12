; Quantified UF route: unsat in 302 instances (2 rounds) once the dispatcher
; consults the quantifier component only at complete assignments. Consulted
; mid-search, the component answered LemmasPending at the first check of every
; round, because quantified assertions are root units; that stop muted EUF
; before the search reached a decision-dependent conflict. With instance
; normalization on it then spent the whole 100,000-instance budget before
; refuting (unknown or timeout under a wall-clock limit). Instance counts are
; deterministic; wall-clock outcomes on this input are not.
; Reduced with ddsmt from SMT-LIB UF grasshopper/uninstantiated/
; find_last_check_heap_access_14_2.smt2, symbols renamed. Declaration and
; assertion order matter: they fix TermId order.
(set-info :status unsat)
(set-logic UF)
(declare-sort Loc 0)
(declare-sort Set 0)
(declare-sort Fld 0)
(declare-fun null () Loc)
(declare-fun ep (Fld Set Loc) Loc)
(declare-fun btwn (Fld Loc Loc Loc) Bool)
(declare-fun in (Loc Set) Bool)
(declare-fun a () Loc)
(declare-fun next () Fld)
(declare-fun fp () Set)
(assert (forall ((v Loc)) (or false (= v null) (not (btwn next null v null)))))
(assert (not (= a null)))
(assert (forall ((s Set) (f Fld) (x Loc) (y Loc)) (or false (not (in y fp)) (in (ep next fp x) fp))))
(assert (forall ((f Fld) (u Loc) (x Loc) (y Loc) (z Loc)) (or false (not (btwn next x y z)) (and (btwn next x y x) (btwn next z z y)))))
(assert (forall ((f Fld) (x Loc) (y Loc) (z Loc)) (or false false (btwn next x y x) (btwn f x z z))))
(check-sat)
