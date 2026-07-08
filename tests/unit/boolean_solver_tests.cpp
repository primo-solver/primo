#include "smt/check_sat.hpp"

#include <cassert>

using primo::smt::CheckSatResult;
using primo::smt::check_sat_smtlib;

namespace {

void pure_boolean_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (or p q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void pure_boolean_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert p)
    (assert (not p))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void implication_is_clausified() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert p)
    (assert (=> p q))
    (assert (not q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void repeated_negation_reuses_the_child_literal() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert (not p))
    (assert (not p))
    (assert p)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void reflexive_equality_is_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (assert (= a a))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void pure_euf_disequality_contradiction_is_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (= a b))
    (assert (not (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void let_bound_terms_are_resolved() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun f (A) A)
    (assert (let ((?x (f a))) (= ?x ?x)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void distinct_lowers_to_pairwise_disequalities() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (assert (distinct a a))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void mixed_boolean_and_euf_is_not_wired_yet() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p () Bool)
    (assert (or p (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

}  // namespace

int main() {
  pure_boolean_sat();
  pure_boolean_unsat();
  implication_is_clausified();
  repeated_negation_reuses_the_child_literal();
  reflexive_equality_is_sat();
  pure_euf_disequality_contradiction_is_unsat();
  let_bound_terms_are_resolved();
  distinct_lowers_to_pairwise_disequalities();
  mixed_boolean_and_euf_is_not_wired_yet();
  return 0;
}
