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
  strict_boundary_is_unsat();
  boolean_structure_over_arithmetic_atoms_can_require_branching();
  handles_disequality_via_boolean_level_case_splitting();
  distinct_on_reals_works();
  return 0;
}
