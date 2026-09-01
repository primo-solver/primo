#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>

#include "smt/check_sat.hpp"

using primo::smt::check_sat_smtlib;
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
  model_combines_integer_values_and_uf_table();
  model_reconstructs_an_integer_substituted_by_a_uf_application();
  unused_integer_uf_gets_a_well_sorted_default();
  rejects_real_symbols();
  return 0;
}
