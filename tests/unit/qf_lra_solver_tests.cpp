#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>

#include "smt/check_sat.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatResult;

namespace {

// Milestone 1 grammar/frontend coverage: no tableau exists yet, so every
// case here either reaches the "QF_LRA theory solving is not yet
// implemented" stub (proving the input parsed and dispatched correctly) or
// is rejected earlier with a specific, distinguishable parse-time message
// (proving grammar restrictions are enforced before ever reaching the
// solver). Update the not-yet-implemented assertions to real Sat/Unsat
// expectations once solve_qf_lra lands.

bool reaches_lra_backend_stub(const primo::smt::CheckSatOutcome& outcome) {
  return outcome.result == CheckSatResult::Unsupported &&
        outcome.message.find("QF_LRA theory solving is not yet implemented") !=
            std::string::npos;
}

void accepts_set_logic_and_real_declarations() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x (* 2 y)) 5))
    (assert (< 0 x))
    (check-sat)
  )");
  assert(reaches_lra_backend_stub(result));
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
  assert(reaches_lra_backend_stub(result));
}

void accepts_nary_minus_and_ratio_literal_coefficient() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (> (- x 1 2) (/ 1 2)))
    (check-sat)
  )");
  assert(reaches_lra_backend_stub(result));
}

void accepts_negative_literal_coefficient_via_unary_minus() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (* (- 1.5) x) 5))
    (check-sat)
  )");
  assert(reaches_lra_backend_stub(result));
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
  assert(reaches_lra_backend_stub(result));
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
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (or (= x y) (<= x 5)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("QF_UFLRA") != std::string::npos);
}

void does_not_silently_treat_arithmetic_atoms_as_opaque_booleans() {
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
  assert(result.result == CheckSatResult::Unsupported);
}

}  // namespace

int main() {
  accepts_set_logic_and_real_declarations();
  accepts_chained_comparisons_and_decimal_literals();
  accepts_nary_minus_and_ratio_literal_coefficient();
  accepts_negative_literal_coefficient_via_unary_minus();
  accepts_coefficient_times_compound_linear_term();
  rejects_two_variable_multiplication();
  rejects_three_operand_multiplication();
  rejects_general_term_division();
  rejects_combined_equality_and_arithmetic();
  does_not_silently_treat_arithmetic_atoms_as_opaque_booleans();
  return 0;
}
