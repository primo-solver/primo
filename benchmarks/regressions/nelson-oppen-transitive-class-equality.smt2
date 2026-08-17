; Not a bug repro -- a deliberately constructed positive fixture for the
; class-equality transitivity shortcut in lra_implied_real_equalities_for_partition
; (src/smt/check_sat.cpp). Eight shared Real terms are each independently
; pinned to 1 by their own bounds (so LRA-preprocessing's fixed-value
; substitution can't collapse them away before Nelson-Oppen sees them as
; distinct shared terms), forming one size-8 LRA model-equal class that is
; genuinely, fully entailed-equal. `f` applied to all eight must then be
; congruent, contradicting `distinct`.
;
; Reach the target code path with --nelson-oppen-prop=exact (bypasses the
; cheaper opportunistic LRA-to-EUF producers so the exact fallback actually
; fires) plus --stats. Expected: unsat, with
; :no.interface.lra-exact-fallback-pairs == 7 (k-1, one probe per class
; member against the anchor) and :no.interface.lra-exact-fallback-transitive-pairs
; == 21 (C(7,2), resolved by transitivity with no probe), together
; accounting for all 28 == C(8,2) entailed equalities -- versus 28 direct
; probes and no transitive pairs before this change.
(set-logic QF_UFLRA)
(declare-fun f (Real) Real)
(declare-const v0 Real)
(declare-const v1 Real)
(declare-const v2 Real)
(declare-const v3 Real)
(declare-const v4 Real)
(declare-const v5 Real)
(declare-const v6 Real)
(declare-const v7 Real)
(assert (>= v0 1)) (assert (<= v0 1))
(assert (>= v1 1)) (assert (<= v1 1))
(assert (>= v2 1)) (assert (<= v2 1))
(assert (>= v3 1)) (assert (<= v3 1))
(assert (>= v4 1)) (assert (<= v4 1))
(assert (>= v5 1)) (assert (<= v5 1))
(assert (>= v6 1)) (assert (<= v6 1))
(assert (>= v7 1)) (assert (<= v7 1))
(assert (distinct (f v0) (f v1) (f v2) (f v3) (f v4) (f v5) (f v6) (f v7)))
(check-sat)
