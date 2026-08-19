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

namespace {

CheckSatOptions small_branch_limit(std::size_t limit) {
  CheckSatOptions options;
  options.lia_branch_limit = limit;
  return options;
}

void trivially_sat_needs_no_branching() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= x 7))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 7)") != std::string::npos);
}

void two_x_equals_three_is_unsat() {
  // 2x == 3 has no integer solution at all: both branches (x<=1, x>=2)
  // contradict the fixed bound 2x==3 immediately.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= (* 2 x) 3))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void one_branch_finds_an_integer_solution() {
  // The LRA relaxation of 2y == x + 1 with 1<=x<=3 can land on a
  // fractional x; one branch-and-bound split resolves it (e.g. x=1,y=1).
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (>= x 1))
    (assert (<= x 3))
    (assert (= (* 2 y) (+ x 1)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  const std::size_t x_pos = result.model.find("(define-fun x () Int ");
  const std::size_t y_pos = result.model.find("(define-fun y () Int ");
  assert(x_pos != std::string::npos);
  assert(y_pos != std::string::npos);
}

void nested_branching_finds_a_solution() {
  // 3x + 5y + 7z == 100 with generous bounds needs the search to resolve
  // more than one fractional variable before landing on an integer point.
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (assert (= (+ (* 3 x) (* 5 y) (* 7 z)) 100))
    (assert (>= x 0))
    (assert (>= y 0))
    (assert (>= z 0))
    (assert (<= x 15))
    (assert (<= y 15))
    (assert (<= z 15))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int ") != std::string::npos);
  assert(result.model.find("(define-fun y () Int ") != std::string::npos);
  assert(result.model.find("(define-fun z () Int ") != std::string::npos);
}

void distinct_over_tight_range_is_unsat() {
  // Pigeonhole: three pairwise-distinct Int constants squeezed into a
  // 2-value domain.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun a () Int)
    (declare-fun b () Int)
    (declare-fun c () Int)
    (assert (>= a 1))
    (assert (<= a 2))
    (assert (>= b 1))
    (assert (<= b 2))
    (assert (>= c 1))
    (assert (<= c 2))
    (assert (distinct a b c))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void distinct_with_enough_room_is_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun a () Int)
    (declare-fun b () Int)
    (declare-fun c () Int)
    (assert (>= a 1))
    (assert (<= a 3))
    (assert (>= b 1))
    (assert (<= b 3))
    (assert (>= c 1))
    (assert (<= c 3))
    (assert (distinct a b c))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
}

// (< x 5) and (> x 3) over Int should tighten to x<=4 and x>=4, pinning
// x=4 exactly -- lra_atom.cpp's strict-inequality tightening, exercised
// end to end through the solver rather than white-box on LraAtom alone.
void strict_inequalities_tighten_to_pin_one_value() {
  const auto sat_result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (< x 5))
    (assert (> x 3))
    (check-sat)
    (get-model)
  )");
  assert(sat_result.result == CheckSatResult::Sat);
  assert(sat_result.model.find("(define-fun x () Int 4)") != std::string::npos);

  const auto unsat_result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (< x 5))
    (assert (> x 3))
    (assert (not (= x 4)))
    (check-sat)
  )");
  assert(unsat_result.result == CheckSatResult::Unsat);
}

// 2x - 2y == 1 has no integer solution (the left side is always even),
// and with x, y both unbounded, plain branch-and-bound never runs out of
// new fractional variables to split on -- the incompleteness
// docs/qf-lia-branch-and-bound.md documents. A small node limit must
// report Unknown rather than hang.
void unbounded_infeasible_system_hits_the_node_limit() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (* 2 x) (+ (* 2 y) 1)))
    (check-sat)
  )",
                                       small_branch_limit(20));
  assert(result.result == CheckSatResult::Unknown);
  assert(result.message.find("node limit") != std::string::npos);
}

void qf_lia_rejects_uninterpreted_functions() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun f (Int) Int)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("uninterpreted functions") != std::string::npos);
}

void qf_lia_rejects_real_constants() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Real)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

void qf_lia_rejects_division() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= (/ x 2) 3))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

void int_constants_require_qf_lia_logic() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Int)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

}  // namespace

int main() {
  trivially_sat_needs_no_branching();
  two_x_equals_three_is_unsat();
  one_branch_finds_an_integer_solution();
  nested_branching_finds_a_solution();
  distinct_over_tight_range_is_unsat();
  distinct_with_enough_room_is_sat();
  strict_inequalities_tighten_to_pin_one_value();
  unbounded_infeasible_system_hits_the_node_limit();
  qf_lia_rejects_uninterpreted_functions();
  qf_lia_rejects_real_constants();
  qf_lia_rejects_division();
  int_constants_require_qf_lia_logic();
  return 0;
}
