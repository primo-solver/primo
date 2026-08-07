#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

#include "smt/check_sat.hpp"
#include "util/statistics.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatOptions;
using primo::smt::CheckSatResult;
using primo::smt::EufEngine;
using primo::smt::LraPivotingRule;
using primo::smt::NelsonOppenPropagationMode;
using primo::util::StatKey;

namespace {

CheckSatOptions no_preprocessing() {
  CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  return options;
}

CheckSatOptions no_preprocessing_or_substitution() {
  CheckSatOptions options = no_preprocessing();
  options.solved_constant_substitution = false;
  return options;
}

CheckSatOptions guarded_real_equality_lowering_options() {
  CheckSatOptions options = no_preprocessing();
  options.guarded_real_equality_lowering = true;
  return options;
}

CheckSatOptions monotone_elimination_options() {
  CheckSatOptions options;
  options.monotone_elimination = true;
  return options;
}

// The model must satisfy the ORIGINAL constraints, which is the only thing
// that makes discharging them legitimate.
void monotone_elimination_reconstructs_a_satisfying_model() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun p () Bool)
    (assert (or (>= x 10) p))
    (check-sat)
    (get-model)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Sat);
  // x occurs only as a lower bound, so it is discharged -- and must come
  // back with a value that actually satisfies the bound it was carrying.
  assert(result.statistics.get(StatKey::MonotoneEliminationVariables) >= 1);
  assert(result.model.find("(define-fun x () Real 11)") != std::string::npos);
}

// Regression: folding an atom discharges it for *every* variable in it, so
// a variable that was itself disqualified (here y, pinned two-sided) can
// lose its last constraint and vanish from the tableau. Reconstruction
// used to skip such bounds as unevaluable and default the eliminated
// variable to zero, producing a model that violated the very constraint
// the pass had discharged.
void monotone_elimination_values_variables_that_lost_all_constraints() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun a () Real)
    (declare-fun b () Real)
    (assert (and (not (<= b (* a (- 1)))) (not (<= b a))))
    (check-sat)
    (get-model)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Sat);
  // b is one-sided (pushed up by both); a is two-sided and disqualified,
  // yet both of its constraints are folded away with b's.
  assert(result.statistics.get(StatKey::MonotoneEliminationVariables) == 1);
  // a defaults to 0, so b must exceed it rather than tie with it.
  assert(result.model.find("(define-fun a () Real 0)") != std::string::npos);
  assert(result.model.find("(define-fun b () Real 0)") == std::string::npos);
}

// A variable pinned from both sides must survive the pass untouched.
void monotone_elimination_keeps_two_sided_variables() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (and (>= x 1) (<= x 5)))
    (check-sat)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::MonotoneEliminationVariables) == 0);
  assert(result.statistics.get(StatKey::MonotoneEliminationAtomsFolded) == 0);
}

// The pass must not change any answer; unsat has to stay unsat.
void monotone_elimination_preserves_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (and (<= x y) (and (<= y 3) (>= x 10))))
    (check-sat)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Unsat);
}

// Two variables pushed apart by the same atoms. Both are one-sided, both
// are discharged, and each shares every atom with the other -- the case a
// dependency-ordering scheme would have mistaken for a cycle. The model
// must still satisfy the strict inequality.
void monotone_elimination_handles_mutually_bounded_variables() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (and (> x y) (<= y x)))
    (check-sat)
    (get-model)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::MonotoneEliminationVariables) == 2);
  // Verified against z3: x = 1, y = 0 satisfies both x > y and y <= x.
  assert(result.model.find("(define-fun x () Real 1)") != std::string::npos);
  assert(result.model.find("(define-fun y () Real 0)") != std::string::npos);
}

// (> x y) and (< y x) desugar to the *same* Lt atom, so one atom carries
// two variables and is discharged once.
void monotone_elimination_handles_duplicated_atom() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (and (> x y) (< y x)))
    (check-sat)
    (get-model)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::MonotoneEliminationVariables) == 2);
  assert(result.model.find("(define-fun x () Real 1)") != std::string::npos);
  assert(result.model.find("(define-fun y () Real 0)") != std::string::npos);
}

// Opposed strict bounds leave both variables two-sided, so nothing is
// discharged and the contradiction survives.
void monotone_elimination_preserves_opposed_strict_bounds() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (and (> x y) (< x y)))
    (check-sat)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(StatKey::MonotoneEliminationVariables) == 0);
}

// A strict and a non-strict bound in the same direction: the reconstructed
// value has to clear the strict one, not merely meet it.
void monotone_elimination_respects_strict_bounds() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (and (< x 5) (<= x 5)))
    (check-sat)
    (get-model)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::MonotoneEliminationVariables) == 1);
  assert(result.model.find("(define-fun x () Real 4)") != std::string::npos);
}

// The core term language has no ITE node, so a Real ITE becomes a fresh
// constant plus guarded equalities. Those equalities pin the fresh constant
// two-sided, while the bounded variable itself stays eliminable -- and its
// reconstructed value must be derived from the fresh constant's model value.
void monotone_elimination_handles_bounds_under_ite() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun p () Bool)
    (assert (<= x (ite p 1 2)))
    (check-sat)
    (get-model)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Sat);
  // Verified against z3: the printed model satisfies the original bound.
  assert(result.model.find("(define-fun x () Real ") != std::string::npos);
}

// One atom reached at both polarities forces its variables two-sided, even
// though each occurrence on its own would look one-sided.
void monotone_elimination_rejects_atom_shared_across_polarities() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (and (or (<= x 5) p) (or (not (<= x 5)) q)))
    (check-sat)
  )",
                                       monotone_elimination_options());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::MonotoneEliminationVariables) == 0);
  assert(result.statistics.get(StatKey::MonotoneEliminationAtomsFolded) == 0);
}

void accepts_set_logic_and_real_declarations() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x (* 2 y)) 5))
    (assert (< 0 x))
    (check-sat)
  )",
                                       no_preprocessing());
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

void accepts_linear_term_division() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (/ x 2) 5))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void routes_combined_equality_and_arithmetic_to_mixed_solver() {
  // Uses an uninterpreted-sort equality alongside a Real comparison -- a
  // genuine EUF+LRA mix. (= x y) between two *Real* terms is itself a pure
  // QF_LRA atom (see real_equality_is_not_misclassified_as_euf below).
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () Real)
    (assert (or (= a b) (<= x 5)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.mixed.euf-atoms") == 1);
  assert(result.statistics.get(":no.mixed.lra-atoms") == 1);
}

void accepts_separable_qf_uf_lra_mix() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () Real)
    (assert (not (= a b)))
    (assert (< 0 x))
    (assert (< x 1))
    (check-sat)
  )",
                                       no_preprocessing());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.mixed.euf-atoms") > 0);
  assert(result.statistics.get(":no.mixed.lra-atoms") > 0);
  assert(result.statistics.get(":no.purification.skipped-no-sites") == 1);
  assert(result.statistics.get(":no.purification.definitions") == 0);
}

void qf_uf_lra_mix_learns_euf_conflicts() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) U)
    (declare-fun x () Real)
    (assert (= a b))
    (assert (not (= (f a) (f b))))
    (assert (< 0 x))
    (check-sat)
  )",
                                       no_preprocessing());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(StatKey::TheoryConflicts) > 0);
}

void qf_uf_lra_honors_theory_conflict_limit() {
  CheckSatOptions options = no_preprocessing();
  options.max_theory_conflicts = 1;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) U)
    (declare-fun x () Real)
    (assert (= a b))
    (assert (not (= (f a) (f b))))
    (assert (< 0 x))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unknown);
  assert(result.statistics.get(StatKey::TheoryConflicts) == 1);
}

void qf_uf_lra_mix_learns_lra_conflicts() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= a b))
    (assert (or (<= (+ x y) 0) (<= (+ x (* 2 y)) 0)))
    (assert (<= 3 x))
    (assert (<= 3 y))
    (check-sat)
  )",
                                       no_preprocessing());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
#if PRIMO_HAVE_IPASIR
  assert(result.statistics.get(":no.interface.lra-online-enabled") == 1);
  assert(result.statistics.get(StatKey::LraOnlineCheckCalls) > 0);
#else
  assert(result.statistics.get(StatKey::LraTheoryConflicts) > 0);
#endif
}

void qf_uf_lra_prints_separable_mixed_model() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () Real)
    (assert (= a b))
    (assert (< 0 x))
    (check-sat)
    (get-model)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(model") != std::string::npos);
  assert(result.model.find("(define-fun a () U (as @uc_") != std::string::npos);
  assert(result.model.find("(define-fun b () U (as @uc_") != std::string::npos);
  assert(result.model.find("(define-fun x () Real ") != std::string::npos);
  assert(result.model.find("$primo_") == std::string::npos);
}

void qf_uf_lra_lra_routed_model_keeps_preprocessed_uf_equalities() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= a b))
    (assert (not (< (+ x y) 5)))
    (assert (< 2 x))
    (assert (not (< y 3)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun a () U (as @uc_") != std::string::npos);
  assert(result.model.find("(define-fun b () U (as @uc_") != std::string::npos);
  assert(result.model.find("(define-fun x () Real ") != std::string::npos);
  assert(result.model.find("(define-fun y () Real ") != std::string::npos);
  assert(result.statistics.get(":solver.qf-lra") == 1);
}

void qf_uf_lra_prints_real_valued_uf_application_model() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun f (U) Real)
    (assert (< 0 (f a)))
    (assert (< (f a) 2))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun f ((x0 U)) Real ") != std::string::npos);
  assert(result.model.find("(ite (= x0 (as @uc_") != std::string::npos);
  assert(result.model.find("$primo_") == std::string::npos);
}

void qf_uf_lra_model_reflects_real_valued_uf_equalities() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) Real)
    (assert (= (f a) (f b)))
    (assert (< 1 (f a)))
    (assert (< (f b) 3))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun f ((x0 U)) Real ") != std::string::npos);
  assert(result.model.find("$primo_") == std::string::npos);
  assert(result.statistics.get(":no.purification.arithmetic-uf-definitions") >= 2);
}

void qf_uf_lra_prints_shared_real_predicate_model() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x y))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
    (get-model)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun p ((x0 Real)) Bool ") != std::string::npos);
  assert(result.model.find("(define-fun x () Real ") != std::string::npos);
  assert(result.model.find("(define-fun y () Real ") != std::string::npos);
  assert(result.statistics.get(":no.interface.model-separations") >= 1);
  assert(result.model.find("$primo_") == std::string::npos);
}

void qf_uf_lra_validates_final_arrangement_without_model() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x y))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.arrangement-checks") >= 1);
  assert(result.statistics.get(":no.interface.arrangement-failures") == 0);
  assert(result.statistics.get(":no.interface.arrangement-separations") >= 1);
  assert(result.statistics.get(":no.interface.model-separation-euf-forbidden-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-separation-euf-mergeable-pairs") == 0);
  assert(result.statistics.get(":no.interface.lra-extra-atom-probes") >= 1);
  assert(result.statistics.get(":no.interface.lra-extra-atoms-probed") >= 1);
}

void qf_uf_lra_final_arrangement_skips_euf_mergeable_shared_pairs() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (p x))
    (assert (p y))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.arrangement-checks") >= 1);
  assert(result.statistics.get(":no.interface.arrangement-failures") == 0);
  assert(result.statistics.get(":no.interface.model-separation-candidates") == 0);
  assert(result.statistics.get(":no.interface.model-separation-euf-mergeable-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-separation-euf-forbidden-pairs") == 0);
  assert(result.statistics.get(":no.interface.arrangement-separations") == 0);
}

void qf_uf_lra_model_reconstruction_skips_euf_mergeable_shared_pairs() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (p x))
    (assert (p y))
    (check-sat)
    (get-model)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(model") != std::string::npos);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.model-separation-candidates") == 0);
  assert(result.statistics.get(":no.interface.model-separation-euf-mergeable-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-separation-euf-forbidden-pairs") == 0);
  assert(result.statistics.get(":no.interface.model-separations") == 0);
  assert(result.model.find("$primo_") == std::string::npos);
}

void qf_uf_lra_final_arrangement_uses_shared_real_interface_terms_only() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun h (Real) U)
    (assert (<= 0 x))
    (assert (not (= (h x) (h y))))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-real-terms-before-interface-filter") == 2);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 1);
  assert(result.statistics.get(":no.interface.model-real-terms-filtered") == 1);
  assert(result.statistics.get(":no.interface.arrangement-checks") >= 1);
  assert(result.statistics.get(":no.interface.model-separation-candidates") == 0);
  assert(result.statistics.get(":no.interface.arrangement-separations") == 0);
}

void qf_uf_lra_model_reconstruction_uses_shared_real_interface_terms_only() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun h (Real) U)
    (assert (<= 0 x))
    (assert (not (= (h x) (h y))))
    (check-sat)
    (get-model)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(model") != std::string::npos);
  assert(result.statistics.get(":no.interface.model-real-terms-before-interface-filter") == 2);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 1);
  assert(result.statistics.get(":no.interface.model-real-terms-filtered") == 1);
  assert(result.statistics.get(":no.interface.model-separation-candidates") == 0);
  assert(result.statistics.get(":no.interface.model-separations") == 0);
}

void qf_uf_lra_final_arrangement_ignores_lra_only_real_terms() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x y))
    (assert (<= z 0))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-real-terms-before-interface-filter") == 2);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.model-real-terms-filtered") == 0);
  assert(result.statistics.get(":no.interface.model-separation-candidates") == 1);
  assert(result.statistics.get(":no.interface.model-separation-euf-forbidden-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-separation-euf-mergeable-pairs") == 0);
}

void qf_uf_lra_prints_shared_real_equal_predicate_model() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (p x))
    (assert (p y))
    (check-sat)
    (get-model)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun p ((x0 Real)) Bool ") != std::string::npos);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.model-separations") == 0);
  assert(result.model.find("$primo_") == std::string::npos);
}

void qf_uf_lra_searches_multiple_shared_real_model_separations() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun h (Real) U)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (<= 0 z))
    (assert (not (= (h x) (h y))))
    (assert (not (= (h x) (h z))))
    (assert (not (= (h y) (h z))))
    (check-sat)
    (get-model)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun h ((x0 Real)) U ") != std::string::npos);
  assert(result.statistics.get(":no.interface.model-separation-candidates") == 3);
  assert(result.statistics.get(":no.interface.model-separations") == 3);
  assert(result.statistics.get(":no.interface.model-separation-search-capped") == 0);
  assert(result.statistics.get(":no.interface.model-separation-search-branches") >= 3);
  assert(result.statistics.get(":no.interface.lra-extra-atom-probes") >= 3);
  assert(result.statistics.get(":no.interface.lra-extra-atoms-probed") >= 3);
  assert(result.model.find("$primo_") == std::string::npos);
}

void qf_uf_lra_orients_capped_model_separations_from_lra_model() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun x0 () Real)
    (declare-fun x1 () Real)
    (declare-fun x2 () Real)
    (declare-fun x3 () Real)
    (declare-fun x4 () Real)
    (declare-fun x5 () Real)
    (declare-fun x6 () Real)
    (declare-fun h (Real) U)
    (assert (= x0 0))
    (assert (= x1 1))
    (assert (= x2 2))
    (assert (= x3 3))
    (assert (= x4 4))
    (assert (= x5 5))
    (assert (= x6 6))
    (assert (not (= (h x0) (h x1))))
    (assert (not (= (h x0) (h x2))))
    (assert (not (= (h x0) (h x3))))
    (assert (not (= (h x0) (h x4))))
    (assert (not (= (h x0) (h x5))))
    (assert (not (= (h x0) (h x6))))
    (assert (not (= (h x1) (h x2))))
    (assert (not (= (h x1) (h x3))))
    (assert (not (= (h x1) (h x4))))
    (assert (not (= (h x1) (h x5))))
    (assert (not (= (h x1) (h x6))))
    (assert (not (= (h x2) (h x3))))
    (assert (not (= (h x2) (h x4))))
    (assert (not (= (h x2) (h x5))))
    (assert (not (= (h x2) (h x6))))
    (assert (not (= (h x3) (h x4))))
    (assert (not (= (h x3) (h x5))))
    (assert (not (= (h x3) (h x6))))
    (assert (not (= (h x4) (h x5))))
    (assert (not (= (h x4) (h x6))))
    (assert (not (= (h x5) (h x6))))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-separation-candidates") == 21);
  assert(result.statistics.get(":no.interface.arrangement-separations") == 21);
  assert(result.statistics.get(":no.interface.model-separation-search-capped") == 1);
  assert(result.statistics.get(":no.interface.model-separation-model-oriented") == 21);
  assert(result.statistics.get(":no.interface.lra-extra-atom-probe-limit-hit") == 0);
}

void qf_uf_lra_arrangement_probe_limit_returns_unknown() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_arrangement_probe_limit = 1;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun h (Real) U)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (<= 0 z))
    (assert (not (= (h x) (h y))))
    (assert (not (= (h x) (h z))))
    (assert (not (= (h y) (h z))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unknown);
  assert(result.message.find("probe limit") != std::string::npos);
  assert(result.statistics.get(":no.interface.lra-extra-atom-probe-limit-hit") == 1);
  assert(result.statistics.get(":no.interface.lra-extra-atom-probes") >= 1);
}

void qf_uf_lra_purifies_nonconstant_real_predicate_arguments() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun p (Real) Bool)
    (assert (p (+ x 1)))
    (assert (< 0 x))
    (check-sat)
  )",
                                       no_preprocessing());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.purification.definitions") == 1);
  assert(result.statistics.get(":no.purification.euf-real-term-definitions") == 1);
  assert(result.statistics.get(":no.mixed.euf-atoms") > 0);
  assert(result.statistics.get(":no.mixed.lra-atoms") > 0);
}

void qf_uf_lra_reuses_nonconstant_real_predicate_argument_proxies() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun q (Real Real) Bool)
    (assert (q (+ x 1) (+ x 1)))
    (check-sat)
  )",
                                       no_preprocessing());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.purification.euf-real-term-definitions") == 1);
  assert(result.statistics.get(":no.purification.reused-real-terms") +
             result.statistics.get(":no.purification.memoized-euf-rewrites") >=
         1);
}

void qf_uf_lra_memoizes_repeated_mixed_purification_rewrites() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun f (U) Real)
    (declare-fun x () Real)
    (assert (and
      (<= (+ (f a) x) 3)
      (<= (+ (f a) x) 3)
      (<= (+ (f a) x) 4)))
    (check-sat)
  )",
                                       no_preprocessing());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.purification.arithmetic-uf-definitions") == 1);
  assert(result.statistics.get(":no.purification.memoized-arithmetic-rewrites") > 0);
}

void qf_uf_lra_shares_purified_real_argument_equalities_with_euf() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= (+ x 1) y))
    (assert (<= y (+ x 1)))
    (assert (p (+ x 1)))
    (assert (not (p y)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.purification.euf-real-term-definitions") == 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") >= 1);
  assert(result.statistics.get(":no.interface.equality-candidates") >= 1);
  assert(result.statistics.get(":no.interface.equality-missing-atoms") >= 1);
  assert(result.statistics.get(":no.interface.equality-created-atoms") >= 1);
  assert(result.statistics.get(":no.interface.equality-clauses") >= 1);
  assert(result.statistics.get(":no.interface.equality-rechecks") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-candidates") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-created-atoms") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-clauses") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-rechecks") >= 1);
  assert(result.statistics.get(":no.interface.equality-local-reasons") >= 1);
  assert(result.statistics.get(":no.interface.equality-local-reason-lits") >= 2);
  assert(result.statistics.get(":no.interface.equality-fallback-reasons") == 0);
}

void qf_uf_lra_prints_model_with_purified_real_predicate_argument() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun p (Real) Bool)
    (assert (<= 0 x))
    (assert (p (+ x 1)))
    (assert (not (p x)))
    (check-sat)
    (get-model)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun p ((x0 Real)) Bool ") != std::string::npos);
  assert(result.model.find("(define-fun x () Real ") != std::string::npos);
  assert(result.model.find("$primo_") == std::string::npos);
  assert(result.statistics.get(":no.purification.euf-real-term-definitions") == 1);
}

void qf_uf_lra_model_keeps_true_purified_real_literal_argument_entry() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun p (Real) Bool)
    (assert (p 0))
    (assert (not (p (+ x 1))))
    (assert (< 0 x))
    (check-sat)
    (get-model)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(ite (= x0 0) true false)") != std::string::npos);
  assert(result.model.find("$primo_") == std::string::npos);
}

void qf_uf_lra_shares_lra_implied_real_equalities_with_euf() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-forbidden-forced") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-skipped") == 0);
  assert(result.statistics.get(":no.interface.conflicts") >= 1);
  assert(result.statistics.get(":no.interface.euf-conflicts") >= 1);
}

void qf_uf_lra_uses_fixed_equalities_before_exact_pair_probes() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x 0))
    (assert (>= x 0))
    (assert (<= y 0))
    (assert (>= y 0))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.interface.lra-opportunistic-fixed-eq") >= 1);
  assert(result.statistics.get(":no.interface.lra-opportunistic-created-atoms") >= 1);
  assert(result.statistics.get(":no.interface.lra-opportunistic-rechecks") >= 1);
  assert(result.statistics.get(":no.interface.lra-opportunistic-reason-lits") >= 4);
  assert(result.statistics.get(":no.interface.lra-exact-fallback-pairs") == 0);
  assert(result.statistics.get(":no.interface.lra-exact-fallback-equalities") == 0);
  assert(result.statistics.get(":no.online.precreated-equality-atoms") == 0);
}

void qf_uf_lra_uses_eqrow_equalities_when_fixed_elimination_exposes_them() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.lra_fixed_elimination = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun s () Real)
    (declare-fun x () Real)
    (declare-fun d () Real)
    (declare-fun p (Real) Bool)
    (assert (= s (+ x d)))
    (assert (<= d 0))
    (assert (>= d 0))
    (assert (p s))
    (assert (not (p x)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.interface.lra-opportunistic-eq-row") >= 1);
  assert(result.statistics.get(":no.interface.lra-opportunistic-created-atoms") >= 1);
  assert(result.statistics.get(":no.interface.lra-opportunistic-reason-lits") >= 2);
  assert(result.statistics.get(":no.interface.lra-exact-fallback-pairs") == 0);
  assert(result.statistics.get(":no.interface.lra-exact-fallback-equalities") == 0);
}

void qf_uf_lra_euf_publishes_a_spanning_set_per_shared_class() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun c () U)
    (declare-fun f (U) Real)
    (assert (= a b))
    (assert (= b c))
    (assert (< (f a) 0))
    (assert (<= (f b) 10))
    (assert (> (f c) 1))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 3);
  assert(result.statistics.get(":no.interface.euf-spanning-equalities") == 2);
  assert(result.statistics.get(":no.interface.euf-to-lra-created-atoms") == 2);
  assert(result.statistics.get(":no.interface.lra-conflicts") >= 1);
}

void qf_uf_lra_reaches_a_convex_equality_fixpoint_across_theories() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun u () U)
    (declare-fun v () U)
    (declare-fun f (U) Real)
    (declare-fun h (Real) U)
    (declare-fun c () Real)
    (declare-fun d () Real)
    (assert (= u v))
    (assert (= (+ (f u) c) (+ (f v) d)))
    (assert (distinct (h c) (h d)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.interface.convex-rounds") >= 2);
  assert(result.statistics.get(":no.interface.euf-to-lra-equalities") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.euf-to-lra-rechecks") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-rechecks") >= 1);
  assert(result.statistics.get(":no.interface.euf-conflicts") >= 1);
}

void qf_uf_lra_candidate_final_scan_propagates_paired_bound_equality() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.qf_uflra_candidate_final_scan = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (p x))
    (assert (not (p y)))
    (assert (q z))
    (assert (< 0 z))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.interface.candidate-final-scan-enabled") == 1);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 3);
  assert(result.statistics.get(":no.interface.candidate-final-scan-pairs") >= 1);
  assert(result.statistics.get(":no.interface.candidate-final-scan-skipped-pairs") >= 1);
  assert(result.statistics.get(":no.interface.candidate-final-scan-equalities") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-forbidden-forced") >= 1);
}

void qf_uf_lra_candidate_final_scan_keeps_noncare_mergeable_pair_in_fallback() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.qf_uflra_candidate_final_scan = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun a () U)
    (declare-fun h (Real) U)
    (declare-fun k (Real) U)
    (assert (<= x z))
    (assert (<= z x))
    (assert (<= y z))
    (assert (<= z y))
    (assert (not (= (h x) a)))
    (assert (not (= (k y) a)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.candidate-final-scan-enabled") == 1);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.candidate-final-scan-pairs") == 0);
  assert(result.statistics.get(":no.interface.candidate-final-scan-skipped-pairs") == 1);
  assert(result.statistics.get(":no.interface.candidate-final-scan-equalities") == 0);
  assert(result.statistics.get(":no.interface.candidate-final-scan-fallbacks") == 0);
  assert(result.statistics.get(":no.interface.candidate-final-scan-fallback-equalities") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-rechecks") == 0);
  assert(result.statistics.get(":no.interface.model-separation-euf-mergeable-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-separation-euf-forbidden-pairs") == 0);
  assert(result.statistics.get(":no.interface.euf-conflicts") == 0);
}

void qf_uf_lra_forces_mergeable_lra_equality_needed_for_congruence() {
  constexpr std::string_view input = R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun f (Real) Real)
    (assert (= x y))
    (assert (= (f x) 0))
    (assert (= (f y) 1))
    (check-sat)
  )";

  const auto result = check_sat_smtlib(input, no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-forced") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-skipped") == 0);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") >= 1);
  assert(result.statistics.get(":no.interface.conflicts") >= 1);

  CheckSatOptions online_options = no_preprocessing_or_substitution();
  online_options.nelson_oppen_propagation = NelsonOppenPropagationMode::Online;
  const auto online_result = check_sat_smtlib(input, online_options);
  assert(online_result.result == CheckSatResult::Unsat);
  assert(online_result.statistics.get(":no.online.final-lra-equality-scan-skipped") == 0);
  assert(online_result.statistics.get(":no.online.propagations") >= 1);
}

void qf_uf_lra_guards_interface_equality_propagation_by_assignment() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (declare-fun choose_separate () Bool)
    (assert (or choose_separate (and (<= x y) (<= y x))))
    (assert choose_separate)
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") == 0);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") == 0);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") == 0);
}

void qf_uf_lra_does_not_share_unentailed_lra_equalities() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x y))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") == 0);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") == 0);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") == 0);
  assert(result.statistics.get(":no.interface.euf-care-full-pairs") == 0);
  assert(result.statistics.get(":no.interface.euf-care-pairs") == 0);
  assert(result.statistics.get(":no.interface.euf-care-pruned-pairs") == 0);
}

void qf_uf_lra_model_equality_branching_backs_off_from_bad_guess() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-equality-api-builds") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-queries") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-equal") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-mutation-unqueried") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-candidates") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-created") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-pending") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-activated") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-assumptions") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-conflicts") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-dropped-assumptions") ==
         result.statistics.get(":no.interface.model-equality-failed-assumptions"));
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
  assert(result.statistics.get(":no.interface.sat-equalities-false") >= 1);
}

void qf_uf_lra_model_equality_mutation_skips_nonforced_bad_guess() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-equality-api-builds") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-queries") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-equal") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-mutation-avoidable") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-lra-equal") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-mutation-checks") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-mutated") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-candidates") == 0);
  assert(result.statistics.get(":no.interface.model-equality-created") == 0);
  assert(result.statistics.get(":no.interface.model-equality-pending") == 0);
  assert(result.statistics.get(":no.interface.model-equality-activated") == 0);
  assert(result.statistics.get(":no.interface.model-equality-assumptions") == 0);
  assert(result.statistics.get(":no.interface.model-equality-conflicts") == 0);
}

void qf_uf_lra_model_equality_forced_by_lra_gets_explained_clause() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_propagation = NelsonOppenPropagationMode::Off;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.interface.model-equality-api-builds") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-mutation-forced") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-mutation-forced") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-forced-propagations") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-forced-clauses") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-forced-created-atoms") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-forced-reason-lits") >= 2);
  assert(result.statistics.get(":no.interface.model-equality-pending") == 0);
  assert(result.statistics.get(":no.interface.model-equality-activated") == 0);
  assert(result.statistics.get(":no.interface.model-equality-assumptions") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") == 0);
}

void qf_uf_lra_model_equality_branching_uses_full_shared_pairs() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (p x))
    (assert (q y))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-equality-api-builds") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-queries") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-equal") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-mutation-unqueried") >= 1);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.euf-care-full-pairs") == 1);
  assert(result.statistics.get(":no.interface.euf-care-pairs") == 0);
  assert(result.statistics.get(":no.interface.euf-care-pruned-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-full-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-care-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-care-pruned-pairs") == 0);
  assert(result.statistics.get(":no.interface.model-equality-candidates") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-created") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-pending") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-activated") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-assumptions") >= 1);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
}

void qf_uf_lra_model_equality_branching_accumulates_active_assumptions() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (declare-fun r (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (<= 0 z))
    (assert (p x))
    (assert (q y))
    (assert (r z))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-equality-max-active-assumptions") >= 2);
  assert(result.statistics.get(":no.interface.model-equality-candidates") >= 2);
  assert(result.statistics.get(":no.interface.model-equality-activated") >= 2);
}

void qf_uf_lra_ranked_model_equality_branching_prefers_care_pairs() {
  // This run rejects one generated equality while another remains active. The
  // prepared EUF atom cache must align assignments by atom identity, not by the
  // round-local true-then-false ordering of generated equalities.
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  options.nelson_oppen_model_equality_branch_policy = primo::smt::ModelEqualityBranchPolicy::Ranked;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (<= 0 z))
    (assert (p x))
    (assert (not (p y)))
    (assert (q z))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.euf-care-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-care-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-care-pruned-pairs") == 2);
  assert(result.statistics.get(":no.interface.model-equality-selected-care") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-conflicts") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-activated") >= 1);
  assert(result.statistics.get(StatKey::SatSolveCalls) < 100);
}

void qf_uf_lra_ranked_model_equality_branching_falls_back_to_full_pairs() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  options.nelson_oppen_model_equality_branch_policy = primo::smt::ModelEqualityBranchPolicy::Ranked;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (p x))
    (assert (q y))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.euf-care-pairs") == 0);
  assert(result.statistics.get(":no.interface.model-equality-care-pairs") == 0);
  assert(result.statistics.get(":no.interface.model-equality-care-pruned-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-selected-care") == 0);
  assert(result.statistics.get(":no.interface.model-equality-selected-full") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-activated") >= 1);
}

void qf_uf_lra_care_queued_model_equality_branching_uses_only_care_pairs() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  options.nelson_oppen_model_equality_branch_policy =
      primo::smt::ModelEqualityBranchPolicy::CareQueued;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (<= 0 z))
    (assert (p x))
    (assert (not (p y)))
    (assert (q z))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-equality-api-builds") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-queries") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-equal") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-mutation-unqueried") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-full-pairs") == 3);
  assert(result.statistics.get(":no.interface.model-equality-care-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-care-pruned-pairs") == 2);
  assert(result.statistics.get(":no.interface.model-equality-queue-builds") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-queued") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-queue-pops") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-selected-care") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-selected-full") == 0);
  assert(result.statistics.get(":no.interface.model-equality-created") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-activated") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-assumptions") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-conflicts") >= 1);
}

void qf_uf_lra_care_queued_model_equality_branching_does_not_fallback_to_full_pairs() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  options.nelson_oppen_model_equality_branch_policy =
      primo::smt::ModelEqualityBranchPolicy::CareQueued;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (p x))
    (assert (q y))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.model-equality-full-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-care-pairs") == 0);
  assert(result.statistics.get(":no.interface.model-equality-care-pruned-pairs") == 1);
  assert(result.statistics.get(":no.interface.model-equality-queue-builds") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-queued") == 0);
  assert(result.statistics.get(":no.interface.model-equality-queue-empty") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-candidates") == 0);
  assert(result.statistics.get(":no.interface.model-equality-created") == 0);
  assert(result.statistics.get(":no.interface.model-equality-pending") == 0);
  assert(result.statistics.get(":no.interface.model-equality-activated") == 0);
  assert(result.statistics.get(":no.interface.model-equality-selected-full") == 0);
}

void qf_uf_lra_care_queued_model_equality_mutation_skips_before_atom_creation() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_branch_policy =
      primo::smt::ModelEqualityBranchPolicy::CareQueued;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-equality-api-builds") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-queries") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-equal") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-api-mutation-avoidable") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-lra-equal") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-mutation-checks") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-mutated") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-queue-skipped-mutation") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-candidates") == 0);
  assert(result.statistics.get(":no.interface.model-equality-created") == 0);
  assert(result.statistics.get(":no.interface.model-equality-pending") == 0);
  assert(result.statistics.get(":no.interface.model-equality-activated") == 0);
}

void qf_uf_lra_adaptive_model_equality_branching_stops_after_bad_guesses() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  options.nelson_oppen_model_equality_branch_policy =
      primo::smt::ModelEqualityBranchPolicy::Adaptive;
  options.nelson_oppen_model_equality_branch_budget = 16;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x0 () Real)
    (declare-fun y0 () Real)
    (declare-fun x1 () Real)
    (declare-fun y1 () Real)
    (declare-fun x2 () Real)
    (declare-fun y2 () Real)
    (declare-fun x3 () Real)
    (declare-fun y3 () Real)
    (declare-fun x4 () Real)
    (declare-fun y4 () Real)
    (declare-fun p (Real) Bool)
    (assert (<= 0 x0))
    (assert (<= 0 y0))
    (assert (<= 0 x1))
    (assert (<= 0 y1))
    (assert (<= 0 x2))
    (assert (<= 0 y2))
    (assert (<= 0 x3))
    (assert (<= 0 y3))
    (assert (<= 0 x4))
    (assert (<= 0 y4))
    (assert (p x0))
    (assert (not (p y0)))
    (assert (p x1))
    (assert (not (p y1)))
    (assert (p x2))
    (assert (not (p y2)))
    (assert (p x3))
    (assert (not (p y3)))
    (assert (p x4))
    (assert (not (p y4)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.model-equality-assumptions") >= 4);
  assert(result.statistics.get(":no.interface.model-equality-adaptive-stops") == 1);
}

void qf_uf_lra_reports_euf_care_pair_diagnostics() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= 0 x))
    (assert (<= 0 y))
    (assert (<= 0 z))
    (assert (p x))
    (assert (not (p y)))
    (assert (q z))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 3);
  assert(result.statistics.get(":no.interface.euf-care-checks") >= 1);
  assert(result.statistics.get(":no.interface.euf-care-full-pairs") == 3);
  assert(result.statistics.get(":no.interface.euf-care-pairs") == 1);
  assert(result.statistics.get(":no.interface.euf-care-pruned-pairs") == 2);
}

void qf_uf_lra_uses_closure_aware_euf_care_pairs_for_nonunary_applications() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_model_equality_branching = true;
  options.nelson_oppen_model_equality_mutation = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun w () Real)
    (declare-fun q (Real Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (<= z w))
    (assert (<= w z))
    (assert (q x z))
    (assert (not (q y w)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 4);
  assert(result.statistics.get(":no.interface.euf-care-full-pairs") == 6);
  assert(result.statistics.get(":no.interface.euf-care-pairs") >= 2);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 2);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 2);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") >= 2);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-forced") >= 2);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-forbidden-forced") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-skipped") == 0);
  assert(result.statistics.get(":no.interface.euf-conflicts") >= 1);
}

void qf_uf_lra_uses_full_shared_pairs_for_lra_to_euf_propagation() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (p x))
    (assert (q y))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.euf-care-full-pairs") == 0);
  assert(result.statistics.get(":no.interface.euf-care-pairs") == 0);
  assert(result.statistics.get(":no.interface.euf-care-pruned-pairs") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-forced") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-skipped") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-forbidden-forced") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-internal-model-equalities") == 0);
}

void qf_uf_lra_model_reconstruction_handles_forced_mergeable_lra_equalities() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.produce_model = true;
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (p x))
    (assert (q y))
    (check-sat)
    (get-model)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(model") != std::string::npos);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-forced") >= 1);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities-euf-mergeable-skipped") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-internal-model-equalities") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-skip-model-reconstruction-fallbacks") ==
         0);
}

void qf_uf_lra_purifies_real_valued_applications_in_arithmetic() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) Real)
    (assert (= a b))
    (assert (< (f a) 1))
    (check-sat)
  )",
                                       no_preprocessing());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.purification.definitions") == 1);
  assert(result.statistics.get(":no.mixed.euf-atoms") > 0);
  assert(result.statistics.get(":no.mixed.lra-atoms") > 0);
}

void qf_uf_lra_shares_euf_implied_proxy_equalities_with_lra() {
  constexpr std::string_view input = R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) Real)
    (assert (= a b))
    (assert (< (f a) 0))
    (assert (< 1 (f b)))
    (check-sat)
  )";

  const auto result = check_sat_smtlib(input, no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.purification.definitions") == 2);
  assert(result.statistics.get(":no.interface.shared-real-terms") == 2);
  assert(result.statistics.get(":no.interface.equalities-propagated") >= 1);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") >= 1);
  assert(result.statistics.get(":no.interface.sat-equalities-forced") >= 1);
  assert(result.statistics.get(":no.interface.euf-to-lra-candidates") >= 1);
  assert(result.statistics.get(":no.interface.euf-to-lra-created-atoms") >= 1);
  assert(result.statistics.get(":no.interface.euf-to-lra-clauses") >= 1);
  assert(result.statistics.get(":no.interface.euf-to-lra-rechecks") >= 1);
  assert(result.statistics.get(":no.interface.equality-local-reasons") >= 1);
  assert(result.statistics.get(":no.interface.equality-local-reason-lits") >= 1);
  assert(result.statistics.get(":no.interface.equality-fallback-reasons") == 0);
  assert(result.statistics.get(":no.interface.euf-explanation-missing") == 0);
  assert(result.statistics.get(":no.interface.conflicts") >= 1);
  assert(result.statistics.get(":no.interface.lra-conflicts") >= 1);

  CheckSatOptions lazy_options = no_preprocessing_or_substitution();
  lazy_options.euf_engine = EufEngine::Lazy;
  const auto lazy_result = check_sat_smtlib(input, lazy_options);
  assert(lazy_result.result == CheckSatResult::Unsat);
  assert(lazy_result.statistics.get(":no.interface.equalities-propagated") >= 1);
  assert(lazy_result.statistics.get(":no.interface.sat-equalities-forced") >= 1);
  assert(lazy_result.statistics.get(":no.interface.euf-to-lra-candidates") >= 1);
  assert(lazy_result.statistics.get(":no.interface.euf-to-lra-clauses") >= 1);
}

void qf_uf_lra_online_propagates_simple_lra_shared_equalities() {
  constexpr std::string_view input = R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (p x))
    (assert (not (p y)))
    (check-sat)
  )";

  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_propagation = NelsonOppenPropagationMode::Online;
  options.qf_uflra_online_euf = true;
  const auto result = check_sat_smtlib(input, options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.online.enabled") == 1);
  assert(result.statistics.get(":no.online.precreated-equality-atoms") == 0);
  assert(result.statistics.get(":no.online.paired-bound-precreate-candidates") == 0);
  assert(result.statistics.get(":no.online.lra-published-equalities") >= 1);
  assert(result.statistics.get(":no.online.lra-bound-equality-candidates") >= 1);
  assert(result.statistics.get(":no.online.lra-bound-equality-missing-atoms") == 0);
  assert(result.statistics.get(":no.online.lra-bound-equality-materialized") >= 1);
  assert(result.statistics.get(":no.online.lazy-equality-atoms-created") >= 1);
  assert(result.statistics.get(":no.online.lazy-equality-vars-observed") >= 1);
  assert(result.statistics.get(":no.online.propagations") >= 1);
  assert(result.statistics.get(":no.online.reason-lits") >= 2);
  assert(result.statistics.get(":no.interface.euf-online-enabled") == 1);
  assert(result.statistics.get(":no.online.euf-external-clauses") >= 1);
  assert(result.statistics.get(":no.online.euf-generated-equalities-registered") >= 1);
  assert(result.statistics.get(":no.online.euf-generated-equality-registration-failures") == 0);
  assert(result.statistics.get(":no.online.euf-generated-equality-observation-failures") == 0);
  assert(result.statistics.get(":no.interface.euf-prepared-atoms") >= 1);
  assert(result.statistics.get(":no.interface.euf-prepared-definitions") >= 1);
}

void qf_uf_lra_online_does_not_materialize_unpublished_shared_pairs() {
  constexpr std::string_view input = R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (<= 0 z))
    (assert (p x))
    (assert (not (p y)))
    (assert (q z))
    (check-sat)
  )";

  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_propagation = NelsonOppenPropagationMode::Online;
  const auto result = check_sat_smtlib(input, options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.online.enabled") == 1);
  assert(result.statistics.get(":no.online.shared-real-pairs") == 3);
  assert(result.statistics.get(":no.online.precreated-equality-atoms") == 0);
  assert(result.statistics.get(":no.interface.euf-online-enabled") == 0);
  assert(result.statistics.get(":no.online.paired-bound-precreate-candidates") == 0);
  assert(result.statistics.get(":no.online.lazy-equality-atoms-created") == 1);
  assert(result.statistics.get(":no.interface.sat-equality-atoms") == 1);
  assert(result.statistics.get(":no.interface.materialized-pairs") == 1);
}

void qf_uf_lra_online_skips_pairs_already_resolved_by_online_propagation() {
  constexpr std::string_view input = R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (p x))
    (assert (q y))
    (check-sat)
  )";

  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_propagation = NelsonOppenPropagationMode::Online;
  const auto result = check_sat_smtlib(input, options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.online.enabled") == 1);
  assert(result.statistics.get(":no.online.precreated-equality-atoms") == 0);
  assert(result.statistics.get(":no.online.paired-bound-precreate-candidates") == 0);
  assert(result.statistics.get(":no.online.final-lra-equality-scan-skipped") == 0);
  assert(result.statistics.get(":no.online.lra-published-equalities") >= 1);
  assert(result.statistics.get(":no.interface.lra-exact-fallback-pairs") == 0);
  assert(result.statistics.get(":no.interface.lra-exact-fallback-skipped-pairs") == 1);
}

void qf_uf_lra_online_scans_non_paired_final_equalities() {
  constexpr std::string_view input = R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun u () Real)
    (declare-fun v () Real)
    (declare-fun p (Real) Bool)
    (declare-fun q (Real) Bool)
    (assert (<= x y))
    (assert (<= y x))
    (assert (= (+ u 1) (+ v 1)))
    (assert (p x))
    (assert (p y))
    (assert (q u))
    (assert (not (q v)))
    (check-sat)
  )";

  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_propagation = NelsonOppenPropagationMode::Online;
  const auto result = check_sat_smtlib(input, options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.online.enabled") == 1);
  assert(result.statistics.get(":no.online.lra-bound-equality-materialized") >= 1);
  assert(result.statistics.get(":no.online.final-lra-equality-scan-skipped") == 0);
  assert(result.statistics.get(":no.online.final-lra-equality-scan-fallbacks") == 0);
  assert(result.statistics.get(":no.interface.arrangement-failures") == 0);
  assert(result.statistics.get(":no.interface.lra-to-euf-equalities") >= 1);
  assert(result.statistics.get(":no.interface.lra-opportunistic-candidates") == 0);
  assert(result.statistics.get(":no.interface.lra-exact-fallback-equalities") >= 1);
  assert(result.statistics.get(":no.interface.conflicts") >= 1);
}

void nelson_oppen_online_option_does_not_wrap_single_theory_lra() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_propagation = NelsonOppenPropagationMode::Online;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= x 1))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 0);
  assert(result.statistics.get(":no.online.enabled") == 0);
}

void nelson_oppen_online_option_does_not_wrap_single_theory_euf() {
  CheckSatOptions options = no_preprocessing_or_substitution();
  options.nelson_oppen_propagation = NelsonOppenPropagationMode::Online;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (assert (= a a))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 0);
  assert(result.statistics.get(":no.online.enabled") == 0);
}

void qf_uf_lra_combines_euf_proxy_equality_with_lowered_real_disequality() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) Real)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= a b))
    (assert (= (f a) x))
    (assert (= (f b) y))
    (assert (not (= x y)))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":preprocessing.real-equality-lowering.lowered") >= 1);
  assert(result.statistics.get(":no.purification.definitions") >= 2);
  assert(result.statistics.get(":no.interface.equalities-propagated") >= 1);
  assert(result.statistics.get(":no.interface.lra-conflicts") >= 1);
}

void qf_uf_lra_purifies_real_valued_application_arithmetic_arguments() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun f (Real) Real)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (< (f (- x y)) 0))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.purification.arithmetic-uf-definitions") == 1);
  assert(result.statistics.get(":no.purification.euf-real-term-definitions") == 1);
}

void qf_uf_lra_purifies_nested_real_valued_application_arguments() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun f (Real) Real)
    (declare-fun g (Real) Real)
    (declare-fun x () Real)
    (assert (< (f (+ (g x) 1)) 0))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":no.purification.arithmetic-uf-definitions") == 2);
  assert(result.statistics.get(":no.purification.euf-real-term-definitions") == 1);
}

void qf_uf_lra_purifies_real_valued_application_disequality_arguments() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun f (Real) Real)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (not (= (f (- x y)) (- (f x)))))
    (check-sat)
  )",
                                       no_preprocessing_or_substitution());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
  assert(result.statistics.get(":preprocessing.real-equality-lowering.lowered") >= 1);
  assert(result.statistics.get(":no.purification.arithmetic-uf-definitions") == 2);
  assert(result.statistics.get(":no.purification.euf-real-term-definitions") >= 1);
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

void qf_lra_rejects_uninterpreted_real_functions() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun f (Real) Real)
    (assert (= (f x) 1))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("uninterpreted functions") != std::string::npos);
}

void qf_lra_rejects_uninterpreted_predicates() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun p (Real) Bool)
    (assert (p x))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("uninterpreted functions") != std::string::npos);
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
  // Exercises LraTableau::check end to end through the SMT-LIB frontend, not
  // just LraTableau's own white-box tests: x+y <= 1 with x, y >= 0 forces
  // x+2y <= 2, contradicting x+2y >= 3, and the two compound polynomials are
  // not proportional so they genuinely occupy separate rows.
  //
  // This used to use x+y <= 1 against x+y >= 3, which item H1b's
  // scale-canonical keys now collapse onto a single shared slack -- making it
  // a direct bound conflict with zero pivots, so it silently stopped covering
  // the pivot path. The :lra.pivots assertion below is there so that can't
  // happen again unnoticed.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x y) 1))
    (assert (>= (+ x (* 2 y)) 3))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(StatKey::LraRows) == 2);
  assert(result.statistics.get(StatKey::LraPivots) > 0);
}

void atoms_sharing_identical_polynomial_solve_correctly() {
  // x<=5 and x<=3 both normalize to the same polynomial ("x"), so
  // PreparedLraTheory maps them onto the same tableau variable -- since item
  // H1a that is `x` itself rather than a shared slack, but the guard is the
  // same either way: both atoms' truth values must still be respected
  // together, not merged/dropped.
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
  // is "right"), so a formula whose every disjunct is theory-false makes it
  // try one, conflict, backtrack, try the other, conflict again, and close
  // the formula UNSAT. Confirms the naive "always guess TRUE first" order
  // doesn't compromise correctness, only decision *order* -- the same
  // pure-performance-lever invariant every other item in this roadmap
  // upholds.
  //
  // The disjuncts are deliberately *compound* polynomials. This test used to
  // use `(or (<= x 0) (>= x 10))` with `3 <= x <= 7`, but item H1 (direct
  // bounds for single-variable atoms) put all four of those atoms on `x`
  // itself, so same-variable propagation now closes that formula with zero
  // decisions -- correct, and a real strengthening, but it stopped exercising
  // cb_decide at all. Compound disjuncts get their own slack variables, whose
  // bounds nothing has asserted until a decision does, so a decision is
  // genuinely required. Both are false for x, y >= 3 (x+y >= 6 and x+2y >= 9),
  // and they are not proportional, so they keep separate slacks.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (or (<= (+ x y) 0) (<= (+ x (* 2 y)) 0)))
    (assert (<= 3 x))
    (assert (<= 3 y))
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

void model_phase_refreshes_from_tableau_assignment() {
  // H5 model-based phase selection is deliberately weaker than
  // --lra-theory-decisions: it never chooses the variable, only the SAT
  // phase. The initial tableau assignment beta is x=0, so the two x <= ...
  // atoms below start FALSE. The unit x <= -2 then moves beta(x) to -2,
  // and the next quiescence point must refresh those phases to TRUE.
  const std::string formula = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= x (- 2)))
    (assert (or (<= x (- 1)) (<= y 0)))
    (check-sat)
  )";

  CheckSatOptions with_model_phase;
  with_model_phase.lra_model_phase = true;
  const auto hinted = check_sat_smtlib(formula, with_model_phase);
  assert(hinted.result == CheckSatResult::Sat);
  assert(hinted.statistics.get(StatKey::LraModelPhaseRefreshes) >= 2);
  assert(hinted.statistics.get(StatKey::LraModelPhaseHints) >
         hinted.statistics.get(StatKey::TheoryAtoms));

  CheckSatOptions without_model_phase;
  const auto unhinted = check_sat_smtlib(formula, without_model_phase);
  assert(unhinted.result == CheckSatResult::Sat);
  assert(unhinted.statistics.get(StatKey::LraModelPhaseRefreshes) == 0);
  assert(unhinted.statistics.get(StatKey::LraModelPhaseHints) == 0);
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
  const auto quiescence_points = result.statistics.get(StatKey::SatDecisions) +
                                 result.statistics.get(StatKey::SatConflicts) + 1;
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

void prints_lra_model_when_requested() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (< 0 x))
    (assert (< x 1))
    (assert (= y (+ x 2)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(model") != std::string::npos);
  assert(result.model.find("(define-fun x () Real (/ 1 4))") != std::string::npos);
  assert(result.model.find("(define-fun y () Real (/ 9 4))") != std::string::npos);
  assert(result.model.find("$primo_") == std::string::npos);
}

// Permuting declare-fun lines must not change the search. The two scripts
// below have identical assertions and differ only in the order the constants
// are declared, so every solver counter has to match.
//
// This is a regression test for a concrete defect. The frontend used to intern
// a term for each declared Real constant at declare-fun time, which assigned
// TermIds in *declaration* order rather than first-use order. Preprocessing's
// Gaussian elimination pivots in ascending TermId order
// (collect_gaussian_elimination_candidates sorts eligible_variables), so
// declaration order silently decided which substitutions were accepted: on the
// chain below, reversing the declarations took solve-eqs from 4 accepted /
// 12 rewrites to 0 / 0, and on miplib/vpm2-0 it moved :lra.pivots between
// 26890 and 12537.
//
// Note the defect was NOT conditional on models being requested, so comparing
// a produce-models run against a plain one does not detect it -- both arms
// move together. Declaration order is the invariant that discriminates.
// (/ t c) with a nonzero rational literal c is linear -- it is (* (1/c) t) --
// so it belongs in QF_LRA even when t is not a literal. Requiring both
// operands to be literals rejected ordinary input; z3 and Yices both accept
// it. Division by a non-literal stays rejected because it really is nonlinear,
// and Yices agrees ("non-linear arithmetic").
void divides_a_linear_term_by_a_rational_literal() {
  const auto solved = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (= (/ x 4) 2))
    (check-sat)
    (get-model)
  )");
  assert(solved.result == CheckSatResult::Sat);
  assert(solved.model.find("(define-fun x () Real 8)") != std::string::npos);

  // Nested inside other arithmetic, and pinning the sign of the reciprocal:
  // x/2 - y/4 > 0 with x < 0 < y needs 2x > y, which is impossible. If the
  // rewrite dropped the sign of the (- 4) divisor this would become x/2 + y/4
  // and flip to sat. z3 and Yices both agree it is unsat.
  const auto signed_nested = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (> (+ (/ x 2) (/ y (- 4))) 0))
    (assert (< x 0))
    (assert (> y 0))
    (check-sat)
  )");
  assert(signed_nested.result == CheckSatResult::Unsat);

  // The same shape is satisfiable once x may be positive (x = 10, y = 1).
  const auto nested = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (> (+ (/ x 2) (/ y (- 4))) 0))
    (assert (> x 0))
    (assert (> y 0))
    (check-sat)
  )");
  assert(nested.result == CheckSatResult::Sat);

  // Unsat must stay unsat: x/2 >= 3 and x <= 5 cannot both hold.
  const auto contradiction = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (>= (/ x 2) 3))
    (assert (<= x 5))
    (check-sat)
  )");
  assert(contradiction.result == CheckSatResult::Unsat);
}

// Replaces the former rejects_general_term_division, which asserted that
// (/ x 2) is unsupported. That pinned a limitation rather than a requirement:
// dividing a linear term by a rational literal is linear. What must still be
// rejected is a non-literal divisor, and division by zero.
void rejects_nonlinear_and_zero_division() {
  const auto nonlinear = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (> (/ x y) 1))
    (check-sat)
  )");
  assert(nonlinear.result == CheckSatResult::Unsupported);

  const auto by_zero = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (> (/ x 0) 1))
    (check-sat)
  )");
  assert(by_zero.result == CheckSatResult::Unsupported);
}

void declaration_order_does_not_change_the_search() {
  const std::string body =
      "(assert (= a (+ b 1)))"
      "(assert (= b (+ c 2)))"
      "(assert (= c (+ d 3)))"
      "(assert (= d (+ e 4)))"
      "(assert (< e 10))"
      "(assert (> a 0))"
      "(check-sat)";
  const std::string forward =
      "(set-logic QF_LRA)"
      "(declare-fun a () Real)(declare-fun b () Real)(declare-fun c () Real)"
      "(declare-fun d () Real)(declare-fun e () Real)" +
      body;
  const std::string reversed =
      "(set-logic QF_LRA)"
      "(declare-fun e () Real)(declare-fun d () Real)(declare-fun c () Real)"
      "(declare-fun b () Real)(declare-fun a () Real)" +
      body;

  const auto first = check_sat_smtlib(forward);
  const auto second = check_sat_smtlib(reversed);
  assert(first.result == CheckSatResult::Sat);
  assert(second.result == CheckSatResult::Sat);
  // Guard the guard: if preprocessing stopped substituting here the test would
  // pass vacuously with 0 == 0.
  assert(first.statistics.get(StatKey::SolvedConstantAccepted) > 0);
  for (const StatKey key : {StatKey::SolvedConstantAccepted, StatKey::SolvedConstantRewrites,
                            StatKey::LraPivots, StatKey::SatDecisions}) {
    assert(first.statistics.get(key) == second.statistics.get(key));
  }
}

void lra_model_reconstructs_preprocessed_solved_constants() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (= x 3))
    (assert (= y (+ x 2)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Real 3)") != std::string::npos);
  assert(result.model.find("(define-fun y () Real 5)") != std::string::npos);
}

void qf_lra_model_requires_get_model_command() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (< 0 x))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.empty());
}

void get_model_requires_produce_models() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (< 0 x))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find(":produce-models true") != std::string::npos);
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

void sparse_pivoting_rule_matches_bland_on_multi_pivot_scenarios() {
  // Mirrors the dantzig/soi variants above, under PivotingRule::Sparse
  // (heuristics-note item H3). The rule prices what a pivot costs; every
  // entering candidate it chooses between repairs the same violation by the
  // same amount, so verdicts must be identical to Bland's -- and the
  // leaving-side knob, which is *not* progress-neutral, must not change them
  // either.
  for (const bool sparse_leaving : {false, true}) {
    CheckSatOptions sparse;
    sparse.lra_pivoting_rule = LraPivotingRule::Sparse;
    sparse.lra_sparse_leaving = sparse_leaving;

    const auto unsat_pivot = check_sat_smtlib(R"(
      (set-logic QF_LRA)
      (declare-fun x () Real)
      (declare-fun y () Real)
      (assert (<= (+ x y) 1))
      (assert (>= (+ x (* 2 y)) 3))
      (assert (>= x 0))
      (assert (>= y 0))
      (check-sat)
    )",
                                              sparse);
    assert(unsat_pivot.result == CheckSatResult::Unsat);
    assert(unsat_pivot.statistics.get(StatKey::LraPivots) > 0);
    // The cost counters must actually move when pivots happen -- they are
    // what any promotion decision for this rule rests on.
    assert(unsat_pivot.statistics.get(StatKey::LraPivotRowUpdates) > 0);
    assert(unsat_pivot.statistics.get(StatKey::LraPivotTermOps) > 0);

    const auto unsat_branching = check_sat_smtlib(R"(
      (set-logic QF_LRA)
      (declare-fun x () Real)
      (assert (or (<= x 0) (>= x 10)))
      (assert (<= 3 x))
      (assert (<= x 7))
      (check-sat)
    )",
                                                  sparse);
    assert(unsat_branching.result == CheckSatResult::Unsat);

    const auto sat_branching = check_sat_smtlib(R"(
      (set-logic QF_LRA)
      (declare-fun x () Real)
      (assert (or (<= x 0) (>= x 10)))
      (assert (= x 10))
      (check-sat)
    )",
                                                sparse);
    assert(sat_branching.result == CheckSatResult::Sat);
  }
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
// -- Item H1: direct bounds for single-variable atoms --
//
// A single-variable atom `c*x ~ rhs` is asserted as a bound on `x` itself, with
// the coefficient divided out and the relation flipped when `c` is negative,
// instead of getting its own slack variable and row. The flip is the
// correctness-critical part (bound_assertion() in check_sat.cpp), so each of
// its four rows gets pinned below in both truth directions.

void single_variable_atoms_share_one_bound_variable() {
  // Both a lower and an upper bound on the same bare variable. These used to
  // normalize to *different* polynomials (coefficient -1 and +1) and so landed
  // on two different slacks, which is exactly what H1 removes: the tableau
  // could not see that x was bounded on both sides without pivoting.
  assert(check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= 0 x))
    (assert (<= x 10))
    (check-sat)
  )")
             .result == CheckSatResult::Sat);

  // Contradictory bounds on one variable must now be a *direct* conflict.
  assert(check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= 5 x))
    (assert (<= x 3))
    (check-sat)
  )")
             .result == CheckSatResult::Unsat);
}

void single_variable_atom_coefficients_are_divided_out() {
  // Positive coefficient: 2x <= 4 means x <= 2, so x >= 3 contradicts it...
  assert(check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (* 2 x) 4))
    (assert (<= 3 x))
    (check-sat)
  )")
             .result == CheckSatResult::Unsat);
  // ...while x >= 1 does not.
  assert(check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (* 2 x) 4))
    (assert (<= 1 x))
    (check-sat)
  )")
             .result == CheckSatResult::Sat);

  // Negative coefficient: -2x <= 4 means x >= -2 (the relation flips), so
  // x <= -3 contradicts it and x <= -1 does not. Getting the flip wrong would
  // silently reverse which models are accepted, which is why both directions
  // are checked rather than only the unsat one.
  assert(check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (* (- 2) x) 4))
    (assert (<= x (- 3)))
    (check-sat)
  )")
             .result == CheckSatResult::Unsat);
  assert(check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (<= (* (- 2) x) 4))
    (assert (<= x (- 1)))
    (check-sat)
  )")
             .result == CheckSatResult::Sat);
}

void single_variable_atom_strictness_survives_the_flip() {
  // bound_assertion()'s four rows, each in both truth directions. `x ~ c` is
  // written directly for the unflipped rows and as `(* (- 1) x) ~ -c` for the
  // flipped ones, so the flip path is what is under test rather than the
  // frontend's own `>=` handling.
  const auto sat_of = [](const std::string& body) {
    return check_sat_smtlib("(set-logic QF_LRA)(declare-fun x () Real)" + body + "(check-sat)")
        .result;
  };

  // x <= 5 TRUE -> upper 5: x = 5 fits, x = 6 does not.
  assert(sat_of("(assert (<= x 5))(assert (<= 5 x))") == CheckSatResult::Sat);
  assert(sat_of("(assert (<= x 5))(assert (<= 6 x))") == CheckSatResult::Unsat);
  // x < 5 TRUE -> upper 5-delta: x = 5 no longer fits.
  assert(sat_of("(assert (< x 5))(assert (<= 5 x))") == CheckSatResult::Unsat);
  assert(sat_of("(assert (< x 5))(assert (< 4 x))") == CheckSatResult::Sat);

  // -x <= -5, i.e. x >= 5 (flipped) TRUE -> lower 5: x = 5 fits, x = 4 does not.
  assert(sat_of("(assert (<= (* (- 1) x) (- 5)))(assert (<= x 5))") == CheckSatResult::Sat);
  assert(sat_of("(assert (<= (* (- 1) x) (- 5)))(assert (<= x 4))") == CheckSatResult::Unsat);
  // -x < -5, i.e. x > 5 (flipped, strict) TRUE -> lower 5+delta: x = 5 is out.
  assert(sat_of("(assert (< (* (- 1) x) (- 5)))(assert (<= x 5))") == CheckSatResult::Unsat);
  assert(sat_of("(assert (< (* (- 1) x) (- 5)))(assert (<= x 6))") == CheckSatResult::Sat);

  // The FALSE direction of each, driven through a negation so the solver has
  // to assert the opposite bound rather than the atom's own.
  // NOT(x <= 5) is x > 5 -> lower 5+delta.
  assert(sat_of("(assert (not (<= x 5)))(assert (<= x 5))") == CheckSatResult::Unsat);
  assert(sat_of("(assert (not (<= x 5)))(assert (<= x 6))") == CheckSatResult::Sat);
  // NOT(x < 5) is x >= 5 -> lower 5.
  assert(sat_of("(assert (not (< x 5)))(assert (<= x 5))") == CheckSatResult::Sat);
  assert(sat_of("(assert (not (< x 5)))(assert (< x 5))") == CheckSatResult::Unsat);
  // NOT(-x <= -5), i.e. NOT(x >= 5), is x < 5 -> upper 5-delta.
  assert(sat_of("(assert (not (<= (* (- 1) x) (- 5))))(assert (<= 5 x))") == CheckSatResult::Unsat);
  assert(sat_of("(assert (not (<= (* (- 1) x) (- 5))))(assert (<= 4 x))") == CheckSatResult::Sat);
  // NOT(-x < -5), i.e. NOT(x > 5), is x <= 5 -> upper 5.
  assert(sat_of("(assert (not (< (* (- 1) x) (- 5))))(assert (<= 5 x))") == CheckSatResult::Sat);
  assert(sat_of("(assert (not (< (* (- 1) x) (- 5))))(assert (< 5 x))") == CheckSatResult::Unsat);
}

void proportional_polynomials_share_one_slack() {
  // Item H1b (the long-deferred roadmap item 3): polynomial keys are
  // scale-canonical, so proportional compound polynomials collapse to one
  // slack instead of getting one each. `x+y <= 5`, `2x+2y <= 10` and
  // `-x-y <= -5` are the same constraint written three ways.
  //
  // Checked structurally via :lra.rows rather than only by answer, because
  // the answer would be the same either way -- what H1b changes is how many
  // rows the tableau needs and whether these atoms can see each other's
  // bounds at all.
  const std::string proportional = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x y) 5))
    (assert (<= (+ (* 2 x) (* 2 y)) 10))
    (assert (<= (+ (* (- 1) x) (* (- 1) y)) (- 5)))
    (check-sat)
  )";
  const auto shared = check_sat_smtlib(proportional);
  assert(shared.result == CheckSatResult::Sat);
  assert(shared.statistics.get(StatKey::LraRows) == 1);

  // Non-proportional compounds must still get their own rows -- the
  // canonicalization must not over-merge.
  const auto separate = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x y) 5))
    (assert (<= (+ x (* 2 y)) 10))
    (check-sat)
  )");
  assert(separate.result == CheckSatResult::Sat);
  assert(separate.statistics.get(StatKey::LraRows) == 2);

  // Sharing a slack must not lose a constraint: scaled forms of *conflicting*
  // bounds still have to conflict.
  assert(check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x y) 5))
    (assert (<= (+ (* (- 2) x) (* (- 2) y)) (- 12)))
    (check-sat)
  )")
             .result == CheckSatResult::Unsat);  // x+y <= 5 and x+y >= 6
}

void tableau_shape_and_rational_statistics_are_published() {
  // These counters are what the whole H1/H2/H3 evaluation rested on -- row
  // counts confirmed H1's mechanism instance by instance, and
  // :rational.big-operations is what settled the small form's width. A
  // counter that silently stopped being published, or drifted from what it
  // claims to count, would invalidate the next measurement without failing
  // anything, so pin them against hand-computable values.
  //
  // Two compound rows (x+y and x+2y) over two problem variables: 4 tableau
  // variables (x, y and one slack each), 2 rows, 4 nonzero coefficients. The
  // bare bounds on x and y add no rows at all -- that is item H1a.
  const auto shaped = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ x y) 5))
    (assert (<= (+ x (* 2 y)) 10))
    (assert (<= 0 x))
    (assert (<= 0 y))
    (check-sat)
  )");
  assert(shaped.result == CheckSatResult::Sat);
  assert(shaped.statistics.get(StatKey::LraTableauVars) == 4);
  assert(shaped.statistics.get(StatKey::LraRows) == 2);
  assert(shaped.statistics.get(StatKey::LraRowNonzeros) == 4);
  // Coefficients this small never leave util::Rational's machine-word form.
  assert(shaped.statistics.get(StatKey::RationalPromotions) == 0);
  assert(shaped.statistics.get(StatKey::RationalBigOperations) == 0);

  // Coefficients well past 64 bits must drive the big-form path, so that a
  // zero here would mean the counter had stopped counting rather than that
  // the fast path was universal.
  const auto big = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ (* 123456789012345678901234567890 x) y) 5))
    (assert (<= (+ x (* 987654321098765432109876543210 y)) 10))
    (assert (<= 1 x))
    (assert (<= 1 y))
    (check-sat)
  )");
  assert(big.result == CheckSatResult::Unsat);
  assert(big.statistics.get(StatKey::RationalBigOperations) > 0);
}

void handles_pathologically_deep_arithmetic_nesting() {
  constexpr int kDepth = 50000;
  std::string formula = "(set-logic QF_LRA)\n(declare-fun x () Real)\n(assert (<= x ";
  for (int i = 0; i < kDepth; ++i) {
    formula += "(+ 1 ";
  }
  formula += "0";
  formula.append(kDepth, ')');
  formula += "))\n(check-sat)\n";

  const auto result = check_sat_smtlib(formula);
  assert(result.result == CheckSatResult::Sat);
}

// Item H7 end to end. The equalities have to sit under a disjunction:
// top-level ones are solved away by Gaussian elimination in preprocessing
// and never reach the tableau at all, so the shape that actually stresses
// H7 is an equality asserted *during* search.
constexpr const char* kFixedEliminationSat = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (or (= (+ x y) 4) (= (+ x y) 7)))
    (assert (or (= (- y z) 1) (= (- y z) 5)))
    (assert (>= x 3))
    (assert (<= z 0))
    (check-sat)
  )";

// Same shape forced unsat: z is pinned at 0, so y is 1 or 5 and x is 3 or 6
// (or -1 or 2), none of which reaches 7.
constexpr const char* kFixedEliminationUnsat = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (or (= (+ x y) 4) (= (+ x y) 7)))
    (assert (or (= (- y z) 1) (= (- y z) 5)))
    (assert (<= z 0))
    (assert (>= z 0))
    (assert (>= x 7))
    (check-sat)
  )";

void fixed_variable_elimination_fires_and_preserves_the_verdict() {
  for (const auto& [formula, expected] :
       {std::pair<const char*, CheckSatResult>{kFixedEliminationSat, CheckSatResult::Sat},
        std::pair<const char*, CheckSatResult>{kFixedEliminationUnsat, CheckSatResult::Unsat}}) {
    CheckSatOptions enabled;
    enabled.lra_fixed_elimination = true;
    const auto with_elimination = check_sat_smtlib(formula, enabled);
    assert(with_elimination.result == expected);
    assert(with_elimination.statistics.get(StatKey::LraFixedVariables) > 0);
    assert(with_elimination.statistics.get(StatKey::LraEliminationPivots) > 0);

    CheckSatOptions disabled;  // off by default -- see CheckSatOptions
    const auto without_elimination = check_sat_smtlib(formula, disabled);
    assert(without_elimination.result == expected);
    assert(without_elimination.statistics.get(StatKey::LraFixedVariables) == 0);
  }
}

void guarded_real_equality_lowering_fires_end_to_end() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun guard () Bool)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (or guard (= z (+ x y))))
    (check-sat)
  )",
                                       guarded_real_equality_lowering_options());

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::RealEqualityLowered) == 1);
  assert(result.statistics.get(StatKey::GuardedRealEqualityDistributed) == 1);
}

void guarded_real_equality_lowering_does_not_strengthen_guarded_sat_branch() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun guard () Bool)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (or guard (= z (+ x y))))
    (assert (not (= z (+ x y))))
    (check-sat)
  )",
                                       guarded_real_equality_lowering_options());

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::RealEqualityLowered) == 2);
  assert(result.statistics.get(StatKey::GuardedRealEqualityDistributed) == 1);
}

void multiple_guarded_real_equalities_use_generic_lowering_end_to_end() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun guard () Bool)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (or guard (= x y) (= y z)))
    (check-sat)
  )",
                                       guarded_real_equality_lowering_options());

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::RealEqualityLowered) == 2);
  assert(result.statistics.get(StatKey::GuardedRealEqualityDistributed) == 0);
}

void guarded_real_equality_lowering_is_default_off_end_to_end() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun guard () Bool)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (assert (or guard (= z (+ x y))))
    (check-sat)
  )",
                                       no_preprocessing());

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(StatKey::RealEqualityLowered) == 1);
  assert(result.statistics.get(StatKey::GuardedRealEqualityDistributed) == 0);
}

}  // namespace

// Interface-equality publishing is Nelson-Oppen machinery and must stay on for
// the mixed path, where the dispatcher enables it. This pins that direction:
// the pure QF_LRA path gets it for free (nothing consumes it there), but
// turning it off for QF_UFLRA would silently lose LRA-to-EUF equality
// propagation. See docs/nelson-oppen-qf-lra-regression.md.
void online_nelson_oppen_still_publishes_lra_interface_equalities() {
  CheckSatOptions options;
  options.nelson_oppen_propagation = NelsonOppenPropagationMode::Online;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun f (Real) Real)
    (assert (<= x y))
    (assert (<= y x))
    (assert (not (= (f x) (f y))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  // x <= y and y <= x force x = y, and only the LRA side can observe that, so
  // a zero here means the publishing path went dead.
  assert(result.statistics.get(":no.online.lra-published-equalities") >= 1);
}

// Dispatch tests for the EUF-predicate classifier (mechanism A of
// docs/nelson-oppen-qf-lra-regression.md). These assert the DISPATCH COUNTER,
// not just the answer: the regression they guard was a formula being solved by
// the wrong solver, which an answer-only test cannot see.

// A propositional variable is not an EUF predicate: no arguments, so no
// congruence rule can apply. Pure QF_LRA plus Booleans must stay on the pure
// path.
void propositional_variables_do_not_force_the_mixed_solver() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (and (or p q) (<= (+ x y) 3) (>= x 1) (>= y 1)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-lra") == 1);
  assert(result.statistics.get(":solver.qf-uflra") == 0);
}

// A Bool-sorted *application* genuinely is an EUF predicate -- congruence
// applies to it -- so it must still reach the mixed solver.
void bool_sorted_application_still_forces_the_mixed_solver() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun f (Real) Bool)
    (assert (and (f x) (not (f y)) (<= x y) (>= x y)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
}

// The unsoundness direction: an uninterpreted sort with arithmetic must reach
// the mixed solver. Narrowing the classifier too far would send this to a
// solver that cannot do congruence.
//
// The equality is deliberately nested under disjunctions. Asserted at top
// level it is a solved-constant candidate, and preprocessing substitutes
// b := a and refutes the formula outright -- no solver runs at all, so the
// dispatch assertion would be vacuous. Verified: that version reports every
// :solver.* counter at 0 with :preprocessing.solve-eqs.accepted 1.
void uninterpreted_sort_with_arithmetic_still_forces_the_mixed_solver() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-sort S 0)
    (declare-fun a () S)
    (declare-fun b () S)
    (declare-fun g (S) S)
    (declare-fun x () Real)
    (assert (or (= a b) (< x 0)))
    (assert (or (not (= a b)) (not (= (g a) (g b)))))
    (assert (<= 0 x))
    (assert (<= x 1))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uflra") == 1);
}

// The case the narrowing could get wrong and nothing else covers: an
// uninterpreted function with a Bool ARGUMENT and a Real result. It sets
// neither has_euf_predicate (the application is Real-sorted) nor, on its own,
// has_equality -- yet the pure LRA solver cannot own the symbol. Whatever the
// dispatch decides, the answer must be right.
void bool_argument_real_result_application_is_not_mis_dispatched() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun k (Bool) Real)
    (assert (= p q))
    (assert (< (k p) 0))
    (assert (< 1 (k q)))
    (check-sat)
  )");
  // k(p) = k(q) by congruence, so k(p) < 0 and k(q) > 1 cannot both hold.
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-lra") == 0);
}

int main() {
  propositional_variables_do_not_force_the_mixed_solver();
  bool_sorted_application_still_forces_the_mixed_solver();
  uninterpreted_sort_with_arithmetic_still_forces_the_mixed_solver();
  bool_argument_real_result_application_is_not_mis_dispatched();
  online_nelson_oppen_still_publishes_lra_interface_equalities();
  accepts_set_logic_and_real_declarations();
  accepts_chained_comparisons_and_decimal_literals();
  accepts_nary_minus_and_ratio_literal_coefficient();
  accepts_negative_literal_coefficient_via_unary_minus();
  accepts_coefficient_times_compound_linear_term();
  recognizes_let_bound_literal_coefficients();
  rejects_two_variable_multiplication();
  rejects_three_operand_multiplication();
  accepts_linear_term_division();
  routes_combined_equality_and_arithmetic_to_mixed_solver();
  accepts_separable_qf_uf_lra_mix();
  qf_uf_lra_mix_learns_euf_conflicts();
  qf_uf_lra_honors_theory_conflict_limit();
  qf_uf_lra_mix_learns_lra_conflicts();
  qf_uf_lra_prints_separable_mixed_model();
  monotone_elimination_reconstructs_a_satisfying_model();
  monotone_elimination_values_variables_that_lost_all_constraints();
  monotone_elimination_keeps_two_sided_variables();
  monotone_elimination_preserves_unsat();
  monotone_elimination_handles_mutually_bounded_variables();
  monotone_elimination_handles_duplicated_atom();
  monotone_elimination_preserves_opposed_strict_bounds();
  monotone_elimination_respects_strict_bounds();
  monotone_elimination_handles_bounds_under_ite();
  monotone_elimination_rejects_atom_shared_across_polarities();
  qf_uf_lra_lra_routed_model_keeps_preprocessed_uf_equalities();
  qf_uf_lra_prints_real_valued_uf_application_model();
  qf_uf_lra_model_reflects_real_valued_uf_equalities();
  qf_uf_lra_prints_shared_real_predicate_model();
  qf_uf_lra_validates_final_arrangement_without_model();
  qf_uf_lra_final_arrangement_skips_euf_mergeable_shared_pairs();
  qf_uf_lra_model_reconstruction_skips_euf_mergeable_shared_pairs();
  qf_uf_lra_final_arrangement_uses_shared_real_interface_terms_only();
  qf_uf_lra_model_reconstruction_uses_shared_real_interface_terms_only();
  qf_uf_lra_final_arrangement_ignores_lra_only_real_terms();
  qf_uf_lra_prints_shared_real_equal_predicate_model();
  qf_uf_lra_searches_multiple_shared_real_model_separations();
  qf_uf_lra_orients_capped_model_separations_from_lra_model();
  qf_uf_lra_arrangement_probe_limit_returns_unknown();
  qf_uf_lra_purifies_nonconstant_real_predicate_arguments();
  qf_uf_lra_reuses_nonconstant_real_predicate_argument_proxies();
  qf_uf_lra_memoizes_repeated_mixed_purification_rewrites();
  qf_uf_lra_shares_purified_real_argument_equalities_with_euf();
  qf_uf_lra_prints_model_with_purified_real_predicate_argument();
  qf_uf_lra_model_keeps_true_purified_real_literal_argument_entry();
  qf_uf_lra_shares_lra_implied_real_equalities_with_euf();
  qf_uf_lra_uses_fixed_equalities_before_exact_pair_probes();
  qf_uf_lra_uses_eqrow_equalities_when_fixed_elimination_exposes_them();
  qf_uf_lra_euf_publishes_a_spanning_set_per_shared_class();
  qf_uf_lra_reaches_a_convex_equality_fixpoint_across_theories();
  qf_uf_lra_candidate_final_scan_propagates_paired_bound_equality();
  qf_uf_lra_candidate_final_scan_keeps_noncare_mergeable_pair_in_fallback();
  qf_uf_lra_forces_mergeable_lra_equality_needed_for_congruence();
  qf_uf_lra_guards_interface_equality_propagation_by_assignment();
  qf_uf_lra_does_not_share_unentailed_lra_equalities();
  qf_uf_lra_model_equality_branching_backs_off_from_bad_guess();
  qf_uf_lra_model_equality_mutation_skips_nonforced_bad_guess();
  qf_uf_lra_model_equality_forced_by_lra_gets_explained_clause();
  qf_uf_lra_model_equality_branching_uses_full_shared_pairs();
  qf_uf_lra_model_equality_branching_accumulates_active_assumptions();
  qf_uf_lra_ranked_model_equality_branching_prefers_care_pairs();
  qf_uf_lra_ranked_model_equality_branching_falls_back_to_full_pairs();
  qf_uf_lra_care_queued_model_equality_branching_uses_only_care_pairs();
  qf_uf_lra_care_queued_model_equality_branching_does_not_fallback_to_full_pairs();
  qf_uf_lra_care_queued_model_equality_mutation_skips_before_atom_creation();
  qf_uf_lra_adaptive_model_equality_branching_stops_after_bad_guesses();
  qf_uf_lra_reports_euf_care_pair_diagnostics();
  qf_uf_lra_uses_closure_aware_euf_care_pairs_for_nonunary_applications();
  qf_uf_lra_uses_full_shared_pairs_for_lra_to_euf_propagation();
  qf_uf_lra_model_reconstruction_handles_forced_mergeable_lra_equalities();
  qf_uf_lra_purifies_real_valued_applications_in_arithmetic();
  qf_uf_lra_shares_euf_implied_proxy_equalities_with_lra();
  qf_uf_lra_online_propagates_simple_lra_shared_equalities();
  qf_uf_lra_online_does_not_materialize_unpublished_shared_pairs();
  qf_uf_lra_online_skips_pairs_already_resolved_by_online_propagation();
  qf_uf_lra_online_scans_non_paired_final_equalities();
  nelson_oppen_online_option_does_not_wrap_single_theory_lra();
  nelson_oppen_online_option_does_not_wrap_single_theory_euf();
  qf_uf_lra_combines_euf_proxy_equality_with_lowered_real_disequality();
  qf_uf_lra_purifies_real_valued_application_arithmetic_arguments();
  qf_uf_lra_purifies_nested_real_valued_application_arguments();
  qf_uf_lra_purifies_real_valued_application_disequality_arguments();
  real_equality_is_not_misclassified_as_euf();
  qf_lra_rejects_uninterpreted_real_functions();
  qf_lra_rejects_uninterpreted_predicates();
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
  model_phase_refreshes_from_tableau_assignment();
  deferred_check_runs_once_per_quiescence_point_not_per_literal();
  strict_boundary_is_unsat();
  prints_lra_model_when_requested();
  divides_a_linear_term_by_a_rational_literal();
  rejects_nonlinear_and_zero_division();
  declaration_order_does_not_change_the_search();
  lra_model_reconstructs_preprocessed_solved_constants();
  qf_lra_model_requires_get_model_command();
  get_model_requires_produce_models();
  boolean_structure_over_arithmetic_atoms_can_require_branching();
  dantzig_pivoting_rule_matches_bland_on_multi_pivot_scenarios();
  sparse_pivoting_rule_matches_bland_on_multi_pivot_scenarios();
  soi_pivoting_rule_matches_bland_on_multi_pivot_scenarios();
  soi_multi_row_conflict_fires_end_to_end();
  handles_disequality_via_boolean_level_case_splitting();
  solve_eqs_fires_for_real_definitional_equality();
  gaussian_elimination_solves_joint_system_by_default();
  gaussian_elimination_can_be_disabled();
  gaussian_elimination_detects_contradictory_system();
  single_variable_atoms_share_one_bound_variable();
  single_variable_atom_coefficients_are_divided_out();
  single_variable_atom_strictness_survives_the_flip();
  proportional_polynomials_share_one_slack();
  tableau_shape_and_rational_statistics_are_published();
  handles_pathologically_deep_arithmetic_nesting();
  distinct_on_reals_works();
  fixed_variable_elimination_fires_and_preserves_the_verdict();
  guarded_real_equality_lowering_fires_end_to_end();
  guarded_real_equality_lowering_does_not_strengthen_guarded_sat_branch();
  multiple_guarded_real_equalities_use_generic_lowering_end_to_end();
  guarded_real_equality_lowering_is_default_off_end_to_end();
  return 0;
}
