#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>

#include "smt/check_sat.hpp"
#include "util/statistics.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatOptions;
using primo::smt::CheckSatResult;
using primo::smt::LraPivotingRule;
using primo::util::StatKey;

namespace {

void accepts_set_logic_and_real_declarations() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x (* 2 y)) 5))
    (assert (< 0 x))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void accepts_chained_comparisons_and_decimal_literals() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (<= x y z))
    (assert (= x 1.5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void accepts_nary_minus_and_ratio_literal_coefficient() {
  // x - 1 - 2 > 1/2  <=>  x > 3.5
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (> (- x 1 2) (/ 1 2)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void accepts_negative_literal_coefficient_via_unary_minus() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (* (- 1.5) x) 5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void accepts_coefficient_times_compound_linear_term() {
  // Stricter than the SMT-LIB QF_LRA grammar (which only allows a bare
  // variable as the non-literal `*` operand), but harmless and still
  // linear -- see the term-kind design note in the plan.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (* 2 (+ x y)) 5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void recognizes_let_bound_literal_coefficients() {
  // Regression test for a real bug found via the fuzz gate: build_times/
  // build_divide used to pattern-match the raw S-expression for a literal
  // operand *before* calling build_term, so a let-bound alias of a literal
  // (e.g. `(let ((c 3)) (* c x))`) wasn't recognized as a coefficient at
  // all -- even though it resolves to one after build_term's own
  // let-binding lookup. fuzzsmt let-binds nearly every literal in its
  // QF_LRA output, so this silently rejected almost all fuzzer-generated
  // input as UnsupportedInput before the fix (build via build_term first,
  // then check the *resulting* term's kind).
  const auto multiplication = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (let ((c 3)) (<= (* c x) 9)))
    (assert (>= x 4))
    (check-sat)
  )");
  assert(multiplication.result == CheckSatResult::Unsat);  // 3*4=12 > 9

  const auto division = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (let ((n 6) (d 2)) (<= (* (/ n d) x) 9)))
    (assert (>= x 4))
    (check-sat)
  )");
  assert(division.result == CheckSatResult::Unsat);  // n/d=3, same as above
}

void rejects_two_variable_multiplication() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (* x y) 5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("literal rational coefficient") != std::string::npos);
}

void rejects_three_operand_multiplication() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (* 2 3 x) 5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("* has wrong arity") != std::string::npos);
}

void rejects_general_term_division() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (/ x 2) 5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("/ is only supported between rational literals") !=
        std::string::npos);
}

void rejects_combined_equality_and_arithmetic() {
  // Uses an uninterpreted-sort equality alongside a Real comparison -- a
  // genuine EUF+LRA mix. (= x y) between two *Real* terms is itself a pure
  // QF_LRA atom (see real_equality_is_not_misclassified_as_euf below), not
  // a case of this rejection.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () Real)
    (assert (or (= a b) (<= x 5)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("QF_UFLRA") != std::string::npos);
}

void real_equality_is_not_misclassified_as_euf() {
  // Regression test for a real bug hit while implementing this milestone:
  // mk_eq is sort-agnostic, so (= x y) on two Real terms builds the same
  // TermKind::Equality node EUF equality uses. Without checking operand
  // sort, contains_equality/has_equality misclassified this as EUF,
  // spuriously tripping the QF_UFLRA rejection above for pure QF_LRA input.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= x y))
    (assert (<= x 5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void detects_direct_bound_conflict() {
  // Regression test for a real bug hit while implementing this milestone:
  // before the has_arithmetic dispatch guard existed, this formula fell
  // through to solve_boolean_conjunction, which treated the two Le atoms as
  // independent Booleans and returned an unsound "sat" (x <= 0 and x >= 1
  // are jointly unsatisfiable over the reals, but satisfiable as
  // independent Booleans).
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= x 0))
    (assert (>= x 1))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void detects_conflict_requiring_an_actual_pivot() {
  // x+y<=1 and x+y>=3 are only discoverable as contradictory via a repair
  // pivot (each atom gets its own slack row, so the tableau doesn't see
  // them as "the same" expression directly) -- exercises LraTableau::check
  // end to end through the SMT-LIB frontend, not just LraTableau's own
  // white-box tests.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x y) 1))
    (assert (>= (+ x y) 3))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void atoms_sharing_identical_polynomial_solve_correctly() {
  // x<=5 and x<=3 both normalize to the exact same polynomial ("x"), so
  // PreparedLraTheory now gives them the same tableau slack/row -- this is
  // a regression guard on that dedup: both atoms' truth values must still
  // be respected together, not merged/dropped.
  const auto satisfiable = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= x 5))
    (assert (<= x 3))
    (assert (>= x 1))
    (check-sat)
  )");
  assert(satisfiable.result == CheckSatResult::Sat);

  const auto unsatisfiable = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= x 2))
    (assert (not (<= x 5)))
    (check-sat)
  )");
  assert(unsatisfiable.result == CheckSatResult::Unsat);
}

void same_slack_propagation_drives_boolean_structure() {
  // (<= x 3) is never asserted directly -- only (<= x 1) is, which tightens
  // the shared "x" slack's upper bound to 1, entailing (<= x 3) TRUE via
  // Milestone 3b's same-slack propagation without ever deciding y. With
  // propagation off, the same formula must still be solved correctly (via
  // a decision instead), so both modes must agree on sat/unsat; only the
  // stats should differ.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= x 5))
    (assert (or (<= x 3) (>= y 1000)))
    (assert (<= x 1))
    (check-sat)
  )";

  CheckSatOptions with_propagation;
  with_propagation.lra_propagation = true;
  const auto propagated = check_sat_smtlib(formula, with_propagation);
  assert(propagated.result == CheckSatResult::Sat);
  assert(propagated.statistics.get(StatKey::LraTheoryPropagations) > 0);

  CheckSatOptions without_propagation;
  without_propagation.lra_propagation = false;
  const auto not_propagated = check_sat_smtlib(formula, without_propagation);
  assert(not_propagated.result == CheckSatResult::Sat);
  assert(not_propagated.statistics.get(StatKey::LraTheoryPropagations) == 0);
}

void same_slack_propagation_can_reach_a_conflict() {
  // (not (<= x 10)) is never asserted directly -- (<= x 1) entails it FALSE
  // (x<=1 implies x<=10), which together with (>= y 1000) being ruled out
  // by (< y 1000) makes the top-level or unsatisfiable via propagation
  // alone, no decisions needed.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= x 5))
    (assert (or (not (<= x 10)) (>= y 1000)))
    (assert (<= x 1))
    (assert (< y 1000))
    (check-sat)
  )";

  CheckSatOptions with_propagation;
  with_propagation.lra_propagation = true;
  const auto propagated = check_sat_smtlib(formula, with_propagation);
  assert(propagated.result == CheckSatResult::Unsat);
  assert(propagated.statistics.get(StatKey::LraTheoryPropagations) > 0);

  CheckSatOptions without_propagation;
  without_propagation.lra_propagation = false;
  const auto not_propagated = check_sat_smtlib(formula, without_propagation);
  assert(not_propagated.result == CheckSatResult::Unsat);
}

void row_based_propagation_drives_boolean_structure() {
  // x's and y's bounds are each directly asserted, but (+ x y) itself
  // never is -- same-slack propagation alone can't entail
  // (<= (+ x y) 10) since no atom directly bounds that polynomial's own
  // slack. Row-based propagation derives (+ x y)'s implied upper bound
  // (2+3=5 <= 10) from x's and y's current bounds, entailing the atom
  // TRUE without ever deciding w/the disjunction.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun w () Real)
    (assert (<= x 2))
    (assert (<= y 3))
    (assert (or (<= (+ x y) 10) (>= w 1000)))
    (check-sat)
  )";

  CheckSatOptions with_row_propagation;
  with_row_propagation.lra_propagation = true;
  with_row_propagation.lra_row_propagation = true;
  const auto propagated = check_sat_smtlib(formula, with_row_propagation);
  assert(propagated.result == CheckSatResult::Sat);
  assert(propagated.statistics.get(StatKey::LraRowPropagations) > 0);
  assert(propagated.statistics.get(StatKey::LraTheoryPropagations) > 0);

  CheckSatOptions without_row_propagation;
  without_row_propagation.lra_propagation = true;
  without_row_propagation.lra_row_propagation = false;
  const auto not_propagated = check_sat_smtlib(formula, without_row_propagation);
  assert(not_propagated.result == CheckSatResult::Sat);
  assert(not_propagated.statistics.get(StatKey::LraRowPropagations) == 0);
}

void bidirectional_row_propagation_drives_boolean_structure() {
  // s := (+ x y) has a directly-asserted lower bound (>= s 5) and x has a
  // directly-asserted upper bound (<= x 2), so x+y>=5 and x<=2 together
  // force y>=3 -- but y's own bound is never directly asserted anywhere.
  // Plain (forward) row-based propagation can't help here -- s's own
  // bound is already directly given, there's nothing to derive about s
  // itself. Only the *reverse* direction (rearranging s's row using s's
  // own bound plus x's bound to derive a bound on y) can entail (<= y 2)
  // FALSE (since y is forced >=3, contradicting y<=2), forcing the
  // disjunction's other branch without ever deciding (<= y 2) directly.
  //
  // The specific atom shapes here aren't arbitrary: single_variable_slack
  // only registers a bare variable's *positive*-coefficient normalized
  // form (`>=` normalizes to a negated/coefficient-(-1) polynomial and
  // never gets an entry at all -- a pre-existing quirk this test must
  // respect, not something item 7 changes), and the specific bound
  // *direction* combination used here (s's lower bound + x's upper
  // bound, deriving y's lower bound) is the one that's actually available
  // given that same quirk -- asserting *both* (<= x 2) and (>= x 2)
  // would split x's bound across two different slacks with only one
  // direction visible on each, making the other combination unreachable.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun w () Real)
    (assert (>= (+ x y) 5))
    (assert (<= x 2))
    (assert (or (<= y 2) (>= w 1000)))
    (check-sat)
  )";

  CheckSatOptions with_bidirectional;
  with_bidirectional.lra_propagation = true;
  with_bidirectional.lra_row_propagation = true;
  with_bidirectional.lra_bidirectional_row_propagation = true;
  const auto propagated = check_sat_smtlib(formula, with_bidirectional);
  assert(propagated.result == CheckSatResult::Sat);
  assert(propagated.statistics.get(StatKey::LraBidirectionalRowPropagations) > 0);
  assert(propagated.statistics.get(StatKey::LraTheoryPropagations) > 0);

  CheckSatOptions forward_only;
  forward_only.lra_propagation = true;
  forward_only.lra_row_propagation = true;
  forward_only.lra_bidirectional_row_propagation = false;
  const auto not_propagated = check_sat_smtlib(formula, forward_only);
  assert(not_propagated.result == CheckSatResult::Sat);
  assert(not_propagated.statistics.get(StatKey::LraBidirectionalRowPropagations) == 0);
}

void bidirectional_row_propagation_can_reach_a_conflict() {
  // Same setup (x+y>=5, x<=2, so y is forced >=3), but now the
  // disjunction's other branch is also directly ruled out -- resolves
  // the whole formula UNSAT via bidirectional propagation deriving
  // (<= y 2) FALSE plus a direct contradiction, no decisions.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun w () Real)
    (assert (>= (+ x y) 5))
    (assert (<= x 2))
    (assert (or (<= y 2) (>= w 1000)))
    (assert (< w 1000))
    (check-sat)
  )";

  CheckSatOptions with_bidirectional;
  with_bidirectional.lra_propagation = true;
  with_bidirectional.lra_row_propagation = true;
  with_bidirectional.lra_bidirectional_row_propagation = true;
  const auto propagated = check_sat_smtlib(formula, with_bidirectional);
  assert(propagated.result == CheckSatResult::Unsat);
  assert(propagated.statistics.get(StatKey::LraBidirectionalRowPropagations) > 0);

  CheckSatOptions forward_only;
  forward_only.lra_propagation = true;
  forward_only.lra_row_propagation = true;
  forward_only.lra_bidirectional_row_propagation = false;
  const auto not_propagated = check_sat_smtlib(formula, forward_only);
  assert(not_propagated.result == CheckSatResult::Unsat);
}

void theory_decisions_drive_boolean_structure() {
  // Competitiveness-roadmap item 8: x's and y's bounds are directly
  // asserted but (+ x y) itself never is, and row-based propagation is
  // off by default -- so the disjunction genuinely needs a decision.
  // Every atom in this formula is arithmetic, so cb_decide should supply
  // every decision itself (:lra.theory-decisions == :sat.decisions) with
  // the flag on, and be entirely inert (0) with it off.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun w () Real)
    (assert (<= x 2))
    (assert (<= y 3))
    (assert (or (<= (+ x y) 10) (>= w 1000)))
    (check-sat)
  )";

  CheckSatOptions with_theory_decisions;
  with_theory_decisions.lra_theory_decisions = true;
  const auto decided = check_sat_smtlib(formula, with_theory_decisions);
  assert(decided.result == CheckSatResult::Sat);
  assert(decided.statistics.get(StatKey::LraTheoryDecisions) > 0);
  assert(decided.statistics.get(StatKey::LraTheoryDecisions) ==
        decided.statistics.get(StatKey::SatDecisions));

  CheckSatOptions without_theory_decisions;
  const auto undecided = check_sat_smtlib(formula, without_theory_decisions);
  assert(undecided.result == CheckSatResult::Sat);
  assert(undecided.statistics.get(StatKey::LraTheoryDecisions) == 0);
}

void theory_decisions_can_reach_a_conflict() {
  // Competitiveness-roadmap item 8: cb_decide always tries an atom's TRUE
  // polarity first (its round-robin order has no notion of which polarity
  // is "right") -- (x<=0 or x>=10) together with 3<=x<=7 rules out BOTH
  // disjuncts, so whichever of the two atoms cb_decide reaches first gets
  // tried TRUE, conflicts immediately against the already-asserted [3,7]
  // bound, and must be flipped via ordinary conflict-driven backtracking
  // before the other disjunct is tried and also conflicts, closing the
  // whole formula UNSAT. Confirms the naive "always guess TRUE first"
  // order doesn't compromise correctness, only decision *order* -- the
  // same pure-performance-lever invariant every other item in this
  // roadmap upholds.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (or (<= x 0) (>= x 10)))
    (assert (<= 3 x))
    (assert (<= x 7))
    (check-sat)
  )";

  CheckSatOptions with_theory_decisions;
  with_theory_decisions.lra_theory_decisions = true;
  const auto decided = check_sat_smtlib(formula, with_theory_decisions);
  assert(decided.result == CheckSatResult::Unsat);
  assert(decided.statistics.get(StatKey::LraTheoryDecisions) > 0);

  CheckSatOptions without_theory_decisions;
  const auto undecided = check_sat_smtlib(formula, without_theory_decisions);
  assert(undecided.result == CheckSatResult::Unsat);
  assert(undecided.statistics.get(StatKey::LraTheoryDecisions) == 0);
}

void row_based_propagation_can_reach_a_conflict() {
  // Same row (+ x y) as above, but the disjunction's other branch is also
  // ruled out (< w 1000), and the shared-polynomial atom is negated --
  // x<=2, y<=3 imply (+ x y)<=5, so NOT(<= (+ x y) 10) is FALSE, closing
  // the whole formula UNSAT via row-based propagation alone, no decisions.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun w () Real)
    (assert (<= x 2))
    (assert (<= y 3))
    (assert (or (not (<= (+ x y) 10)) (>= w 1000)))
    (assert (< w 1000))
    (check-sat)
  )";

  CheckSatOptions with_row_propagation;
  with_row_propagation.lra_propagation = true;
  with_row_propagation.lra_row_propagation = true;
  const auto propagated = check_sat_smtlib(formula, with_row_propagation);
  assert(propagated.result == CheckSatResult::Unsat);
  assert(propagated.statistics.get(StatKey::LraRowPropagations) > 0);

  CheckSatOptions without_row_propagation;
  without_row_propagation.lra_propagation = true;
  without_row_propagation.lra_row_propagation = false;
  const auto not_propagated = check_sat_smtlib(formula, without_row_propagation);
  assert(not_propagated.result == CheckSatResult::Unsat);
}

void deferred_check_runs_once_per_quiescence_point_not_per_literal() {
  // Roadmap item 6: OnlineLraPropagator::notify_assignment_lit used to
  // call tableau_.check() eagerly after every single assigned literal
  // (decisions *and* propagated ones). It now defers to
  // cb_has_external_clause, which CaDiCaL only asks once per quiescence
  // point (once per decision plus once per conflict). Same-slack
  // propagation on x's shared slack means asserting (<= x 1) entails
  // (<= x 2), (<= x 3), and (<= x 4) directly TRUE, satisfying each `or`
  // clause's left disjunct without ever needing y1/y2/y3's own values --
  // three extra arithmetic-atom assignments (LraTheoryPropagations) that
  // must NOT each trigger their own check() call.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y1 () Real)
    (declare-fun y2 () Real)
    (declare-fun y3 () Real)
    (assert (<= x 1))
    (assert (or (<= x 2) (>= y1 1000)))
    (assert (or (<= x 3) (>= y2 1000)))
    (assert (or (<= x 4) (>= y3 1000)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::LraTheoryPropagations) > 0);
  // The actual point: check() runs at most once per quiescence point
  // (one before the first decision, then one more per decision/conflict)
  // -- not once per arithmetic-atom assignment, however many of those a
  // single quiescence point's propagation ends up delivering.
  const auto quiescence_points =
      result.statistics.get(StatKey::SatDecisions) + result.statistics.get(StatKey::SatConflicts) + 1;
  assert(result.statistics.get(StatKey::LraOnlineCheckCalls) <= quiescence_points);
}

void strict_boundary_is_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (< x 5))
    (assert (>= x 5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void boolean_structure_over_arithmetic_atoms_can_require_branching() {
  // (x<=0 or x>=10) together with 3<=x<=7 rules out both disjuncts.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (or (<= x 0) (>= x 10)))
    (assert (<= 3 x))
    (assert (<= x 7))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);

  const auto satisfiable = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (or (<= x 0) (>= x 10)))
    (assert (= x 10))
    (check-sat)
  )");
  assert(satisfiable.result == CheckSatResult::Sat);
}

// Milestone 4: the Dantzig pivoting rule (opt-in via CheckSatOptions, not
// the default) must reach the same sat/unsat answers as Bland's rule on
// scenarios that actually require pivots -- re-uses
// detects_conflict_requiring_an_actual_pivot's and
// boolean_structure_over_arithmetic_atoms_can_require_branching's exact
// formulas above.
void dantzig_pivoting_rule_matches_bland_on_multi_pivot_scenarios() {
  CheckSatOptions dantzig;
  dantzig.lra_pivoting_rule = LraPivotingRule::Dantzig;

  const auto unsat_pivot = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x y) 1))
    (assert (>= (+ x y) 3))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )",
                                            dantzig);
  assert(unsat_pivot.result == CheckSatResult::Unsat);

  const auto unsat_branching = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (or (<= x 0) (>= x 10)))
    (assert (<= 3 x))
    (assert (<= x 7))
    (check-sat)
  )",
                                                dantzig);
  assert(unsat_branching.result == CheckSatResult::Unsat);

  const auto sat_branching = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (or (<= x 0) (>= x 10)))
    (assert (= x 10))
    (check-sat)
  )",
                                              dantzig);
  assert(sat_branching.result == CheckSatResult::Sat);
}

void soi_pivoting_rule_matches_bland_on_multi_pivot_scenarios() {
  // Mirrors dantzig_pivoting_rule_matches_bland_on_multi_pivot_scenarios
  // exactly, under PivotingRule::Soi (competitiveness-roadmap item 4).
  CheckSatOptions soi;
  soi.lra_pivoting_rule = LraPivotingRule::Soi;

  const auto unsat_pivot = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x y) 1))
    (assert (>= (+ x y) 3))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )",
                                            soi);
  assert(unsat_pivot.result == CheckSatResult::Unsat);

  const auto unsat_branching = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (or (<= x 0) (>= x 10)))
    (assert (<= 3 x))
    (assert (<= x 7))
    (check-sat)
  )",
                                                soi);
  assert(unsat_branching.result == CheckSatResult::Unsat);

  const auto sat_branching = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (or (<= x 0) (>= x 10)))
    (assert (= x 10))
    (check-sat)
  )",
                                              soi);
  assert(sat_branching.result == CheckSatResult::Sat);
}

void soi_multi_row_conflict_fires_end_to_end() {
  // x+y<=10 and y+z<=10 both violated simultaneously (x>=8,y>=5,z>=8),
  // sharing y -- exercises the multi-row conflict + QuickXplain path
  // through the full solver stack, not just LraTableau directly.
  CheckSatOptions soi;
  soi.lra_pivoting_rule = LraPivotingRule::Soi;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (<= (+ x y) 10))
    (assert (<= (+ y z) 10))
    (assert (>= x 8))
    (assert (>= z 8))
    (assert (>= y 5))
    (check-sat)
  )",
                                       soi);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(StatKey::LraSoiMultiRowConflicts) > 0);
}

void handles_disequality_via_boolean_level_case_splitting() {
  // Real is a total order, so t1 = t2 <=> t1 <= t2 AND t2 <= t1 -- Frontend
  // desugars Real equality to that conjunction at parse time (mk_theory_equality),
  // so NOT(t1=t2) is just NOT(AND(Le,Le)), handled by BooleanClausifier's
  // ordinary Tseitin/CNF machinery. No case-split/lemma code exists in
  // LraTableau or OnlineLraPropagator; CDCL branching over the two
  // underlying Le atoms does the case-splitting for free. This used to be
  // explicitly rejected ("QF_LRA disequality ... is not yet supported");
  // now supported without any tableau-level changes at all.
  const auto satisfiable = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (not (= x 3)))
    (check-sat)
  )");
  assert(satisfiable.result == CheckSatResult::Sat);

  const auto satisfiable_with_bounds = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (not (= x 3)))
    (assert (<= 2 x))
    (assert (<= x 4))
    (check-sat)
  )");
  assert(satisfiable_with_bounds.result == CheckSatResult::Sat);

  const auto unsatisfiable = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (not (= x 3)))
    (assert (<= 3 x))
    (assert (<= x 3))
    (check-sat)
  )");
  assert(unsatisfiable.result == CheckSatResult::Unsat);
}

void distinct_on_reals_works() {
  const auto satisfiable = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (distinct x y z))
    (assert (<= 0 x)) (assert (<= x 1))
    (assert (<= 0 y)) (assert (<= y 1))
    (assert (<= 0 z)) (assert (<= z 1))
    (check-sat)
  )");
  assert(satisfiable.result == CheckSatResult::Sat);

  const auto unsatisfiable = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (distinct x y))
    (assert (= x 1))
    (assert (= y 1))
    (check-sat)
  )");
  assert(unsatisfiable.result == CheckSatResult::Unsat);
}

void solve_eqs_fires_for_real_definitional_equality() {
  // Regression test for the redesign that moved Real-equality desugaring
  // from parse time to a late lower_real_equalities preprocessing pass:
  // solve-eqs now sees a genuine TermKind::Equality node for `(= x 5)`
  // (previously this was a complete no-op for QF_LRA, silently, since the
  // Real equality had already become And(Le,Le) before this pass ran).
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= x 5))
    (assert (<= (+ x y) 10))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::SolvedConstantAccepted) > 0);
  assert(result.statistics.get(StatKey::SolvedConstantRewrites) > 0);
}

void gaussian_elimination_solves_joint_system_by_default() {
  // 2x+3y=5 and x-y=1 jointly solve for x=8/5, y=3/5; neither equation
  // alone is a bare-variable definition, so this only fires via Gaussian
  // elimination, not the ordinary solve-eqs recognizer. Default-on since
  // 2026-07-25's campaign-tier Slurm confirmation (job 113407: 3000/3000
  // fuzzsmt cases + opfuzz/yinyang mutation shards, 0 crashes/wrong
  // answers/bug triggers).
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= (+ (* 2 x) (* 3 y)) 5))
    (assert (= (- x y) 1))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::SolvedConstantGaussianCandidates) > 0);
}

void gaussian_elimination_can_be_disabled() {
  CheckSatOptions without_gaussian;
  without_gaussian.gaussian_elimination = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= (+ (* 2 x) (* 3 y)) 5))
    (assert (= (- x y) 1))
    (check-sat)
  )",
                                       without_gaussian);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::SolvedConstantGaussianCandidates) == 0);
}

void gaussian_elimination_detects_contradictory_system() {
  // Same left-hand polynomial, different right-hand constants -- neither
  // equation alone is bare-variable-shaped, so this contradiction is only
  // ever caught by the ordinary LRA tableau, not by solve-eqs itself
  // (the Gaussian pass has no special contradiction detection -- see the
  // roadmap design note). This confirms end-to-end soundness either way.
  CheckSatOptions with_gaussian;
  with_gaussian.gaussian_elimination = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= (+ (* 2 x) (* 3 y)) 5))
    (assert (= (+ (* 2 x) (* 3 y)) 6))
    (check-sat)
  )",
                                       with_gaussian);
  assert(result.result == CheckSatResult::Unsat);
}

// Frontend::build_term (TermBuilder in check_sat.cpp) is iterative, not
// recursive descent -- see the TermBuilder class comment. A deeply nested
// `+` chain like this used to stack-overflow (found via fuzzing under
// ASAN, where larger stack frames made even fuzzer-typical nesting depths
// unsafe); this synthetic depth is far beyond anything a native recursive
// implementation could tolerate even in a Release build.
void handles_pathologically_deep_arithmetic_nesting() {
  constexpr int kDepth = 50000;
  std::string formula =
      "(set-logic QF_LRA)\n(declare-fun x () Real)\n(assert (<= x ";
  for (int i = 0; i < kDepth; ++i) {
    formula += "(+ 1 ";
  }
  formula += "0";
  formula.append(kDepth, ')');
  formula += "))\n(check-sat)\n";

  const auto result = check_sat_smtlib(formula);
  assert(result.result == CheckSatResult::Sat);
}

}  // namespace

int main() {
  accepts_set_logic_and_real_declarations();
  accepts_chained_comparisons_and_decimal_literals();
  accepts_nary_minus_and_ratio_literal_coefficient();
  accepts_negative_literal_coefficient_via_unary_minus();
  accepts_coefficient_times_compound_linear_term();
  recognizes_let_bound_literal_coefficients();
  rejects_two_variable_multiplication();
  rejects_three_operand_multiplication();
  rejects_general_term_division();
  rejects_combined_equality_and_arithmetic();
  real_equality_is_not_misclassified_as_euf();
  detects_direct_bound_conflict();
  detects_conflict_requiring_an_actual_pivot();
  atoms_sharing_identical_polynomial_solve_correctly();
  same_slack_propagation_drives_boolean_structure();
  same_slack_propagation_can_reach_a_conflict();
  row_based_propagation_drives_boolean_structure();
  row_based_propagation_can_reach_a_conflict();
  bidirectional_row_propagation_drives_boolean_structure();
  bidirectional_row_propagation_can_reach_a_conflict();
  theory_decisions_drive_boolean_structure();
  theory_decisions_can_reach_a_conflict();
  deferred_check_runs_once_per_quiescence_point_not_per_literal();
  strict_boundary_is_unsat();
  boolean_structure_over_arithmetic_atoms_can_require_branching();
  dantzig_pivoting_rule_matches_bland_on_multi_pivot_scenarios();
  soi_pivoting_rule_matches_bland_on_multi_pivot_scenarios();
  soi_multi_row_conflict_fires_end_to_end();
  handles_disequality_via_boolean_level_case_splitting();
  solve_eqs_fires_for_real_definitional_equality();
  gaussian_elimination_solves_joint_system_by_default();
  gaussian_elimination_can_be_disabled();
  gaussian_elimination_detects_contradictory_system();
  handles_pathologically_deep_arithmetic_nesting();
  distinct_on_reals_works();
  return 0;
}
