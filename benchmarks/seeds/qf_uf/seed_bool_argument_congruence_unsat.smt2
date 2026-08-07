; UNSAT purely by congruence over Bool-sorted arguments, and containing no
; equality over any other sort. That combination is what made the dispatch
; send it to the Boolean solver, which has no congruence rule -- so before the
; fix this answered `sat`. Dropping congruence can only ever produce a
; spurious `sat`, which is why a satisfiable seed cannot expose the bug and
; this one can. See docs/bool-argument-congruence-bug.md.
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun r () Bool)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(assert (= p q))
(assert (or r (not (= (f p) (f q)))))
(assert (or (not r) (not (= (g p r) (g q r)))))
(check-sat)
