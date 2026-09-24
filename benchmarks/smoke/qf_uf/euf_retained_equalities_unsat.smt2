; Root equalities must remain available whichever disjunct SAT explores.
; The deterministic callback regression fixes the actual backtrack sequence;
; this input checks the complete solver without prescribing its search order.
(set-logic QF_UF)
(set-info :status unsat)
(declare-sort U 0)
(declare-fun a () U)
(declare-fun b () U)
(declare-fun c () U)
(declare-fun d () U)
(declare-fun e () U)
(assert (= a b))
(assert (= b c))
(assert (or (= c d) (= c e)))
(assert (not (= a d)))
(assert (not (= a e)))
(check-sat)
