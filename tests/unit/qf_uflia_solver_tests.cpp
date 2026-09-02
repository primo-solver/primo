#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>

#include "smt/check_sat.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatOptions;
using primo::smt::CheckSatResult;

namespace {

void accepts_integer_uninterpreted_functions() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= (f x) (+ x 1)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflia") == 1);
  assert(result.statistics.get(":combination.components") == 2);
  assert(result.statistics.contains(":time.qf-uflia.sat-us"));
  assert(result.statistics.contains(":time.qf-uflia.assignment-extraction-us"));
  assert(result.statistics.contains(":time.qf-uflia.euf-check-us"));
  assert(result.statistics.contains(":time.qf-uflia.lia-check-us"));
  assert(result.statistics.contains(":time.qf-uflia.lia-rational-us"));
  assert(result.statistics.contains(":time.qf-uflia.lia-integer-us"));
}

// LIA is non-convex: 0 <= x <= 1 entails x=0 or x=1, but neither equality
// alone.  EUF derives both disequalities by congruence from the two function
// result disequalities.  Complete SAT-visible arrangement search must therefore
// reject both branches rather than wait for one individually entailed equality.
void non_convex_integer_arrangement_is_complete() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun f (Int) Int)
    (assert (>= x 0))
    (assert (<= x 1))
    (assert (= y 0))
    (assert (= z 1))
    (assert (not (= (f x) (f y))))
    (assert (not (= (f x) (f z))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":combination.arrangement-pairs") >= 3);
  assert(result.statistics.get(":combination.learned-clauses") >= 1);
}

void model_guidance_handles_a_non_convex_disjunction_with_reversible_choices() {
  CheckSatOptions options;
  options.experimental_qf_uflia_model_combination = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun f (Int) Int)
    (assert (>= x 0))
    (assert (<= x 1))
    (assert (= y 0))
    (assert (= z 1))
    (assert (not (= (f x) (f y))))
    (assert (not (= (f x) (f z))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":combination.model.enabled") == 1);
  assert(result.statistics.get(":combination.model.decisions") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-blocking-clauses") >= 1);
  assert(result.statistics.get(":combination.model.materialized-pairs") <
         result.statistics.get(":combination.model.total-pairs"));
}

void model_guidance_has_a_complete_arrangement_fallback() {
  CheckSatOptions options;
  options.experimental_qf_uflia_model_combination = true;
  options.qf_uflia_model_decision_budget = 0;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun f (Int) Int)
    (assert (<= x y))
    (assert (not (= (f x) (f y))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":combination.model.complete-fallbacks") == 1);
  assert(result.statistics.get(":combination.model.fallback-budget") == 1);
  assert(result.statistics.get(":combination.model.fallback-unsupported") == 0);
  assert(result.statistics.get(":combination.model.fallback-unknown") == 0);
  assert(result.statistics.get(":combination.model.fallback-resource-limit") == 0);
  assert(result.statistics.contains(":time.qf-uflia.complete-fallback-us"));
  assert(result.statistics.get(":combination.model.materialized-pairs") ==
         result.statistics.get(":combination.model.total-pairs"));
}

void agreeing_component_models_avoid_the_complete_arrangement() {
  CheckSatOptions options;
  options.experimental_qf_uflia_model_combination = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= (f x) (+ x 1)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":combination.model.total-pairs") == 1);
  assert(result.statistics.get(":combination.model.materialized-pairs") == 0);
  assert(result.statistics.get(":combination.model.snapshots") == 2);
}

void model_uses_the_arithmetic_value_of_a_purified_uf_argument() {
  CheckSatOptions options;
  options.experimental_qf_uflia_model_combination = true;
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-sort S 0)
    (declare-fun a () S)
    (declare-fun b () S)
    (declare-fun x () Int)
    (declare-fun f (Int) S)
    (assert (not (= a b)))
    (assert (< x 1))
    (assert (= (f (+ (* 4 x) 1)) a))
    (check-sat)
    (get-model)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 0)") != std::string::npos);
  assert(result.model.find("(= x0 1) (as @uc_3_0 S)") != std::string::npos);
}

void model_combines_integer_values_and_uf_table() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= x 2))
    (assert (= (f x) 3))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 2)") != std::string::npos);
  assert(result.model.find("(define-fun f ((x0 Int)) Int") != std::string::npos);
  assert(result.model.find("(= x0 2) 3") != std::string::npos);
}

void model_reconstructs_an_integer_substituted_by_a_uf_application() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun f (Int) Int)
    (assert (= (f y) x))
    (assert (<= x 2))
    (assert (< 3 (+ x y)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  // This test is paired with direct Dolmen validation in the QF_UFLIA fuzz
  // gate. The old reconstruction printed x outside its asserted upper bound.
  assert(result.model.find("(define-fun x () Int 0)") != std::string::npos ||
         result.model.find("(define-fun x () Int 1)") != std::string::npos ||
         result.model.find("(define-fun x () Int 2)") != std::string::npos ||
         result.model.find("(define-fun x () Int (-") != std::string::npos);
}

// Solved-constant elimination runs before mixed purification. The recorded
// definition therefore contains f(a), while the LIA tableau contains only the
// later proxy equated to f(a). Reconstruction must carry that proxy value
// through the final EUF class before evaluating the compound definition.
void model_reconstructs_a_compound_definition_with_a_uf_application() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun a () Int)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= a 2))
    (assert (= (f a) 2))
    (assert (= x (+ (- 7) (* 4 (f a)))))
    (assert (< x 2))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 1)") != std::string::npos);
  assert(result.model.find("(= x0 2) 2") != std::string::npos);
}

void unused_integer_uf_gets_a_well_sorted_default() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun f (Int) Int)
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun f ((x0 Int)) Int 0)") != std::string::npos);
}

void rejects_real_symbols() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun f (Int) Real)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);

  const auto real_argument = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun f (Real) Int)
    (check-sat)
  )");
  assert(real_argument.result == CheckSatResult::Unsupported);
}

}  // namespace

int main() {
  accepts_integer_uninterpreted_functions();
  non_convex_integer_arrangement_is_complete();
  model_guidance_handles_a_non_convex_disjunction_with_reversible_choices();
  model_guidance_has_a_complete_arrangement_fallback();
  agreeing_component_models_avoid_the_complete_arrangement();
  model_uses_the_arithmetic_value_of_a_purified_uf_argument();
  model_combines_integer_values_and_uf_table();
  model_reconstructs_an_integer_substituted_by_a_uf_application();
  model_reconstructs_a_compound_definition_with_a_uf_application();
  unused_integer_uf_gets_a_well_sorted_default();
  rejects_real_symbols();
  return 0;
}
