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

CheckSatOptions branch_and_cut_limits(std::size_t branch_limit, std::size_t cut_limit) {
  CheckSatOptions options;
  options.lia_branch_limit = branch_limit;
  options.lia_gomory_cut_limit = cut_limit;
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

// docs/open-leads.md lead 12a: bare-form solved-constant substitution
// (x=t, x not occurring in t) is enabled by default for QF_LIA. y=5 and
// x=y+3 chains across two of the substitution loop's passes -- the first
// pass accepts y=5 (no other pending candidate depends on it) and
// rewrites x=y+3 to x=5+3 in the same pass; the second pass then accepts
// x=5+3 directly. Both eliminated variables never reach the tableau at
// all (PreparedLraTheory::variables_by_term has no entry for either), so
// this exercises build_qf_lia_model's substitution-aware fallback
// (evaluate_qf_lia_model_term), not just the rewrite itself -- the bug
// this same lead found and fixed defaulted an eliminated variable's
// model value to 0 instead of resolving it.
void chained_solved_constant_substitution_reconstructs_the_model() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= y 5))
    (assert (= x (+ y 3)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 8)") != std::string::npos);
  assert(result.model.find("(define-fun y () Int 5)") != std::string::npos);
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

// 2x - 2y == 1 has no integer solution (the left side is always even).
// Stage 2's GCD-consistency check (docs/qf-lia-branch-and-bound.md)
// catches this directly from the tableau row, without ever invoking
// branch-and-bound -- so this must come back Unsat immediately even under
// a node limit far too small for branch-and-bound alone to ever reach
// Unsat on its own (see lia_branch_and_bound_tests.cpp's
// unbounded_polytope_hits_the_node_limit for that fact, confirmed at the
// LraTableau level).
void gcd_inconsistent_system_is_unsat_without_branching() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (* 2 x) (+ (* 2 y) 1)))
    (check-sat)
  )",
                                       small_branch_limit(1));
  assert(result.result == CheckSatResult::Unsat);
}

// 4x + 4y in [2,3], x and y unbounded: GCD-consistency does not fire here
// (the row is bounded on both sides, not fixed to a single value, and
// 2 and 3 are consecutive integers -- no single fixed residual to check
// divisibility against), yet no integer x, y satisfies it (4x+4y is
// always a multiple of 4, and no multiple of 4 lies in [2,3]). This is
// exactly the kind of instance stage 2 deliberately does NOT close: the
// LRA relaxation stays feasible at every branch-and-bound node (real x,y
// can pick 4x+4y anywhere in [2,3]), so the search never runs out of new
// fractional variables to split on and must report Unknown rather than
// hang, matching docs/qf-lia-branch-and-bound.md's documented remaining
// incompleteness after stage 2.
void gcd_uncatchable_divergent_system_hits_the_node_limit() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (>= (+ (* 4 x) (* 4 y)) 2))
    (assert (<= (+ (* 4 x) (* 4 y)) 3))
    (check-sat)
  )",
                                       small_branch_limit(20));
  assert(result.result == CheckSatResult::Unknown);
  assert(result.message.find("node limit") != std::string::npos);
}

// 3x + 5y = 7, x,y >= 0: no non-negative integer solution exists (y=0
// needs x=7/3; y=1 needs 3x=2; y>=2 makes 5y>7 alone), confirmed
// independently against z3 (unsat). The LP relaxation is feasible
// (e.g. x=7/3, y=0), so branch-and-bound alone needs to actually branch
// to discover the infeasibility -- with a branch limit of 1 it cannot,
// and reports Unknown (stage 1's own honest incompleteness). Stage 3's
// Gomory cut pre-check resolves this from the very first candidate model
// without branch-and-bound needing to explore anything, demonstrating
// cuts genuinely extend what stage 1+2 alone could resolve within the
// same node budget -- the concrete, user-visible point of this stage.
void gomory_cut_resolves_what_branch_and_bound_alone_cannot() {
  const char* const problem = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (+ (* 3 x) (* 5 y)) 7))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )";

  const auto without_cuts = check_sat_smtlib(problem, branch_and_cut_limits(1, 0));
  assert(without_cuts.result == CheckSatResult::Unknown);
  assert(without_cuts.message.find("node limit") != std::string::npos);

  const auto with_cuts = check_sat_smtlib(problem, branch_and_cut_limits(1, 100));
  assert(with_cuts.result == CheckSatResult::Unsat);
}

// docs/qf-lia-branch-and-bound.md's "Conflict generalization" section.
// `3x + 5y = 7, x,y >= 0` is the same instance the Gomory-cut section uses:
// unsat, LRA-feasible, and closed by a cut rather than by branching. Here it
// pins the *lemma* the cut produces rather than the answer.
//
// The z and w constraints are deliberately irrelevant to the contradiction:
// they are satisfiable on their own and share no variable with it, so they
// are exactly the kind of atom the coarse clause names and the derivation
// does not. Without them the instance is too small to distinguish the two
// forms -- all four of its atoms genuinely participate in the cut, so the
// derived lemma is the same size as the coarse one and the test would prove
// nothing. With them, the coarse clause is 8 literals and the derivation is
// still 4.
void conflict_generalization_narrows_the_lemma_to_the_derivation() {
  const std::string script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun w () Int)
    (assert (= (+ (* 3 x) (* 5 y)) 7))
    (assert (>= x 0))
    (assert (>= y 0))
    (assert (>= z 0))
    (assert (<= z 10))
    (assert (>= w 5))
    (assert (<= w 20))
    (check-sat)
  )";
  const auto result = check_sat_smtlib(script, CheckSatOptions{});
  assert(result.result == CheckSatResult::Unsat);

  const auto generalized = result.statistics.get(primo::util::StatKey::LiaGeneralizedLemmas);
  const auto fallbacks = result.statistics.get(primo::util::StatKey::LiaGeneralizationFallbacks);
  const auto derived_lits =
      result.statistics.get(primo::util::StatKey::LiaGeneralizedLemmaLiterals);
  const auto coarse_lits = result.statistics.get(primo::util::StatKey::LiaCoarseLemmaLiterals);
  assert(generalized > 0);
  assert(fallbacks == 0);
  assert(derived_lits < coarse_lits);
}

// The switch has to be a pure quality knob: turning generalization off
// restores the coarse clause and must not change any answer. Asserted on
// both an unsat instance (where the lemma is what drives the outer loop) and
// a sat one (where no lemma is emitted at all), since a bad lemma shows up
// as either a wrong answer or a non-terminating loop.
void conflict_generalization_does_not_change_answers() {
  const std::string unsat_script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (+ (* 3 x) (* 5 y)) 7))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )";
  const std::string sat_script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (<= 3 (+ x y)))
    (assert (<= (+ x y) 4))
    (assert (distinct x y))
    (check-sat)
  )";
  CheckSatOptions coarse;
  coarse.lia_conflict_generalization = false;
  const CheckSatOptions generalized;

  assert(check_sat_smtlib(unsat_script, generalized).result == CheckSatResult::Unsat);
  assert(check_sat_smtlib(unsat_script, coarse).result == CheckSatResult::Unsat);
  assert(check_sat_smtlib(sat_script, generalized).result == CheckSatResult::Sat);
  assert(check_sat_smtlib(sat_script, coarse).result == CheckSatResult::Sat);

  // With generalization off, every lemma is the coarse one, by definition.
  const auto coarse_result = check_sat_smtlib(unsat_script, coarse);
  assert(coarse_result.statistics.get(primo::util::StatKey::LiaGeneralizedLemmas) == 0);
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
  chained_solved_constant_substitution_reconstructs_the_model();
  two_x_equals_three_is_unsat();
  one_branch_finds_an_integer_solution();
  nested_branching_finds_a_solution();
  distinct_over_tight_range_is_unsat();
  distinct_with_enough_room_is_sat();
  strict_inequalities_tighten_to_pin_one_value();
  gcd_inconsistent_system_is_unsat_without_branching();
  gcd_uncatchable_divergent_system_hits_the_node_limit();
  gomory_cut_resolves_what_branch_and_bound_alone_cannot();
  conflict_generalization_narrows_the_lemma_to_the_derivation();
  conflict_generalization_does_not_change_answers();
  qf_lia_rejects_uninterpreted_functions();
  qf_lia_rejects_real_constants();
  qf_lia_rejects_division();
  int_constants_require_qf_lia_logic();
  return 0;
}
