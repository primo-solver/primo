#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>

#include "smt/check_sat.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatResult;

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

void rejects_disequality() {
  // Negated Real equality (x != 3) needs case-splitting (x<3 or x>3), not
  // representable as a single bound -- explicitly out of scope for this
  // milestone, see find_lra_theory_conflict's Eq-relation handling.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (not (= x 3)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("disequality") != std::string::npos);
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
  strict_boundary_is_unsat();
  boolean_structure_over_arithmetic_atoms_can_require_branching();
  rejects_disequality();
  return 0;
}
