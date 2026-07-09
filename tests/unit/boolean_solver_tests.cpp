#include <cassert>

#include "smt/check_sat.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatResult;

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
  assert(result.statistics.get(":input.assertions") == 1);
  assert(result.statistics.get(":preprocessing.passes") == 1);
  assert(result.statistics.get(":solver.boolean") == 1);
  assert(result.statistics.get(":solver.qf-uf") == 0);
  assert(result.statistics.get(":sat.solve-calls") == 1);
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

void mixed_boolean_and_euf_can_be_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p () Bool)
    (assert (or p (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void mixed_boolean_and_euf_conflict_is_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p () Bool)
    (assert (or p (= a b)))
    (assert (not p))
    (assert (not (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void predicate_applications_respect_congruence() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p (A) Bool)
    (assert (= a b))
    (assert (p a))
    (assert (not (p b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void annotated_assertions_are_solved() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort Z 0)
    (declare-fun circuit () Bool)
    (declare-fun input () Bool)
    (declare-fun reg () Bool)
    (declare-fun flash () Z)
    (declare-fun nf () Z)
    (assert (! circuit :named hyp1))
    (assert (! (= nf flash) :named hyp2))
    (assert (! (not (or (and input (not reg)) (not input) reg)) :named goal))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void equality_bridge_solves_diamond_chain() {
  primo::smt::CheckSatOptions options;
  options.eq_bridge = true;
  options.preprocessing_passes = 3;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun x0 () A)
    (declare-fun y0 () A)
    (declare-fun z0 () A)
    (declare-fun x1 () A)
    (declare-fun y1 () A)
    (declare-fun z1 () A)
    (declare-fun x2 () A)
    (assert (and
      (or (and (= x0 y0) (= y0 x1)) (and (= x0 z0) (= z0 x1)))
      (or (and (= x1 y1) (= y1 x2)) (and (= x1 z1) (= z1 x2)))
      (not (= x0 x2))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":preprocessing.passes") >= 1);
  assert(result.statistics.get(":preprocessing.eq-bridge.equalities") >= 2);
  assert(result.statistics.get(":preprocessing.unit-equality.consequences") >= 1);
  assert(result.statistics.get(":solver.qf-uf") == 1);
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
  mixed_boolean_and_euf_can_be_sat();
  mixed_boolean_and_euf_conflict_is_unsat();
  predicate_applications_respect_congruence();
  annotated_assertions_are_solved();
  equality_bridge_solves_diamond_chain();
  return 0;
}
