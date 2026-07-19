#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

#include "smt/check_sat.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatResult;

namespace {

std::string read_text_file(const char* path) {
  std::ifstream input(path);
  assert(input.good());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

primo::smt::CheckSatOptions propagation_test_options() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  options.uf_symmetry_breaking = false;
  options.eq_bridge = false;
  options.euf_propagation_interval = 1;
  options.euf_propagation_assign_threshold = 1.0;
  return options;
}

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

void top_level_conjunction_does_not_allocate_auxiliary() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (assert (and p q r))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.boolean-units.facts") >= 3);
  assert(result.statistics.get(":sat.vars") == 3);
  assert(result.statistics.get(":sat.clauses") == 3);
  assert(result.statistics.get(":sat.unit-clauses") == 3);
}

void top_level_or_uses_tseitin_support_clauses_by_default() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  const auto encoded = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (or p q))
    (check-sat)
  )",
      options);
  assert(encoded.result == CheckSatResult::Sat);
  assert(encoded.statistics.get(":sat.vars") == 3);
  assert(encoded.statistics.get(":sat.clauses") == 4);
  assert(encoded.statistics.get(":sat.binary-clauses") == 2);
  assert(encoded.statistics.get(":sat.unit-clauses") == 1);

  options.top_level_or_tseitin = false;
  const auto flat = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (or p q))
    (check-sat)
  )",
      options);
  assert(flat.result == CheckSatResult::Sat);
  assert(flat.statistics.get(":sat.vars") == 2);
  assert(flat.statistics.get(":sat.clauses") == 1);
  assert(flat.statistics.get(":sat.binary-clauses") == 1);
  assert(flat.statistics.get(":sat.unit-clauses") == 0);
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

void implication_is_right_associative() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (assert p)
    (assert q)
    (assert (=> p q r))
    (assert (not r))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void xor_is_clausified() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert p)
    (assert q)
    (assert (xor p q))
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

void boolean_equality_is_propositional_equivalence() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (= p q))
    (assert p)
    (assert (not q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":preprocessing.boolean-units.facts") >= 2);
  assert(result.statistics.get(":solver.boolean") == 0);
  assert(result.statistics.get(":solver.qf-uf") == 0);
}

void self_contradictory_boolean_equality_is_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert (= p (not p)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void boolean_distinct_is_propositional_xor() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (distinct p q))
    (assert p)
    (assert q)
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

void default_euf_propagation_auto_uses_online_for_small_abstractions() {
  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (= a b))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.auto-online") == 1);
  assert(result.statistics.get(":euf.propagation.auto-off") == 0);
}

void default_euf_propagation_auto_uses_conflict_for_large_abstractions() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun a0 () A)\n";
  for (int i = 1; i <= 5001; ++i) {
    input << "(declare-fun a" << i << " () A)\n";
    input << "(assert (= a0 a" << i << "))\n";
  }
  input << "(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.auto-conflict") == 1);
  assert(result.statistics.get(":euf.propagation.auto-online") == 0);
  assert(result.statistics.get(":euf.propagation.auto-off") == 0);
}

void default_euf_propagation_auto_uses_online_for_large_finite_domain_shapes() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun v0 () A)\n";
  input << "(declare-fun v1 () A)\n";
  for (int i = 0; i < 501; ++i) {
    input << "(declare-fun x" << i << " () A)\n";
    input << "(assert (or (= x" << i << " v0) (= x" << i << " v1)))\n";
  }
  for (int i = 0; i < 4000; ++i) {
    input << "(declare-fun d" << i << " () A)\n";
  }
  input << "(assert (and (distinct v0 v1)";
  for (int i = 0; i < 501; ++i) {
    input << " (or (= x" << i << " v0) (= x" << i << " v1))";
  }
  input << " (or";
  for (int i = 0; i < 4000; ++i) {
    input << " (= d" << i << " v0)";
  }
  input << ")))\n";
  input << "(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") >= 500);
  assert(result.statistics.get(":euf.theory-atoms") > 5000);
  assert(result.statistics.get(":euf.propagation.auto-online") == 1);
  assert(result.statistics.get(":euf.propagation.auto-conflict") == 0);
}

void default_euf_propagation_auto_keeps_conflict_for_negative_heavy_finite_domain_shapes() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun v0 () A)\n";
  input << "(declare-fun v1 () A)\n";
  for (int i = 0; i < 501; ++i) {
    input << "(declare-fun x" << i << " () A)\n";
    input << "(assert (or (= x" << i << " v0) (= x" << i << " v1)))\n";
  }
  for (int i = 0; i < 4000; ++i) {
    input << "(declare-fun d" << i << " () A)\n";
  }
  for (int i = 0; i < 600; ++i) {
    input << "(declare-fun n" << i << " () A)\n";
  }
  input << "(assert (and (distinct v0 v1)";
  for (int i = 0; i < 501; ++i) {
    input << " (or (= x" << i << " v0) (= x" << i << " v1))";
  }
  for (int i = 0; i < 600; ++i) {
    input << " (not (= n" << i << " v0))";
  }
  input << " (or";
  for (int i = 0; i < 4000; ++i) {
    input << " (= d" << i << " v0)";
  }
  input << ")))\n";
  input << "(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") >= 500);
  assert(result.statistics.get(":euf.pure-atoms.negative") >
         result.statistics.get(":preprocessing.finite-domain.domains"));
  assert(result.statistics.get(":euf.theory-atoms") > 5000);
  assert(result.statistics.get(":euf.propagation.auto-online") == 0);
  assert(result.statistics.get(":euf.propagation.auto-conflict") == 1);
}

void chainable_equality_is_expanded() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b c))
    (assert (not (= a c)))
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

void define_fun_macros_are_expanded() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (define-fun same ((x A) (y A)) Bool (= x y))
    (define-fun aa () A a)
    (assert (same aa b))
    (assert (not (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void compound_bool_arguments_are_bound_before_euf_flattening() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun wrap (Bool) A)
    (assert p)
    (assert (not (= (wrap (or p q)) (wrap (or p q)))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void recursive_define_fun_is_rejected() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (define-fun loop () Bool loop)
    (assert loop)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

void boolean_ite_is_eliminated() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun c () Bool)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert c)
    (assert p)
    (assert (not (ite c p q)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void term_ite_is_eliminated() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun c () Bool)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () U)
    (assert c)
    (assert (= x (ite c a b)))
    (assert (not (= x a)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":frontend.ite.auxiliaries") == 0);
}

void true_boolean_constant_used_as_uf_argument_is_euf_term() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun f (Bool) U)
    (assert p)
    (assert (not (= (f p) (f true))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void false_boolean_constant_used_as_uf_argument_is_euf_term() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun f (Bool) U)
    (assert (not p))
    (assert (not (= (f p) (f false))))
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
}

void ground_unit_substitution_option_records_rewrites() {
  primo::smt::CheckSatOptions options;
  options.ground_unit_substitution = true;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (= (f a) b))
    (assert (or (= (f a) c) (not (= b c))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.unit-equality.substitution-units") >= 1);
  assert(result.statistics.get(":preprocessing.unit-equality.substitutions") >= 1);
  assert(result.statistics.get(":preprocessing.unit-equality.substitution-accepted") >= 1);
}

void guarded_ground_unit_substitution_rejects_tiny_rewrite() {
  primo::smt::CheckSatOptions options;
  options.guarded_ground_unit_substitution = true;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (= (f a) b))
    (assert (not (= b c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.unit-equality.substitutions") == 0);
  assert(result.statistics.get(":preprocessing.unit-equality.substitution-rejected") >= 1);
}

void solved_constant_substitution_records_rewrites_by_default() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a (f b)))
    (assert (= (f a) (f b)))
    (check-sat)
  )");

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.solve-eqs.candidates") >= 1);
  assert(result.statistics.get(":preprocessing.solve-eqs.accepted") >= 1);
  assert(result.statistics.get(":preprocessing.solve-eqs.rewrites") >= 1);
}

void solved_constant_substitution_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a (f b)))
    (assert (= (f a) (f b)))
    (check-sat)
  )",
                                       options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.solve-eqs.candidates") == 0);
  assert(result.statistics.get(":preprocessing.solve-eqs.accepted") == 0);
  assert(result.statistics.get(":preprocessing.solve-eqs.rewrites") == 0);
}

void auto_preprocessing_bridges_single_assertion_without_finite_domain_expansion() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Auto;
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
  assert(result.statistics.get(":preprocessing.auto.single-assertion") == 1);
  assert(result.statistics.get(":preprocessing.auto.strong") == 0);
  assert(result.statistics.get(":preprocessing.eq-bridge.equalities") >= 2);
  assert(result.statistics.get(":preprocessing.equality-definitions") == 0);
}

void auto_preprocessing_skips_large_flat_assertion_batches() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Auto;
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-fun p () Bool)\n";
  for (int i = 0; i < 10001; ++i) {
    input << "(assert p)\n";
  }
  input << "(check-sat)\n";

  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":input.assertions") == 10001);
  assert(result.statistics.get(":preprocessing.auto.large-assertion-skip") == 1);
  assert(result.statistics.get(":preprocessing.passes") == 0);
}

void preprocessing_off_disables_basic_loop() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert p)
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.passes") == 0);
}

void preprocessing_extends_when_passes_keep_shrinking_terms() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Basic;
  options.preprocessing_passes = 1;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q0 () Bool)
    (declare-fun q1 () Bool)
    (declare-fun q2 () Bool)
    (declare-fun q3 () Bool)
    (declare-fun q4 () Bool)
    (assert (and p
      (or p q0)
      (or p q1)
      (or p q2)
      (or p q3)
      (or p q4)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.adaptive-passes") == 1);
  assert(result.statistics.get(":preprocessing.passes") == 2);
  assert(result.statistics.get(":preprocessing.term-size.final") <
         result.statistics.get(":preprocessing.term-size.initial"));
}

void preprocessing_adaptive_extra_passes_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Basic;
  options.preprocessing_passes = 1;
  options.preprocessing_adaptive_extra_passes = 0;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q0 () Bool)
    (declare-fun q1 () Bool)
    (assert (and p (or p q0) (or p q1)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.adaptive-passes") == 0);
  assert(result.statistics.get(":preprocessing.passes") == 1);
}

void positive_pure_theory_literal_is_filtered_from_euf_check() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.euf_engine = primo::smt::EufEngine::Lazy;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun c () U)
    (declare-fun p () Bool)
    (assert p)
    (assert (= a c))
    (assert (= c b))
    (assert (or (= a b) p))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.pure-atoms.positive") >= 1);
  assert(result.statistics.get(":euf.pure-literals.filtered") >= 1);
  assert(result.statistics.get(":sat.solve-calls") == 1);
}

void pure_theory_literal_filter_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.euf_engine = primo::smt::EufEngine::Lazy;
  options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Off;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun c () U)
    (declare-fun p () Bool)
    (assert p)
    (assert (= a c))
    (assert (= c b))
    (assert (or (= a b) p))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.pure-filter.enabled") == 0);
  assert(result.statistics.get(":euf.pure-atoms.positive") >= 1);
  assert(result.statistics.get(":euf.pure-literals.filtered") == 0);
}

void mixed_polarity_theory_literal_is_not_filtered() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.euf_engine = primo::smt::EufEngine::Lazy;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun c () U)
    (declare-fun p () Bool)
    (assert p)
    (assert (= a c))
    (assert (= c b))
    (assert (or (= a b) p))
    (assert (or (not (= a b)) p))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.pure-literals.filtered") == 0);
}

void strong_preprocessing_enables_finite_domain_amo() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-choices") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.post-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.deduped-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.ground-congruence-clauses") == 0);
  assert(result.statistics.get(":euf.propagation.rounds") == 0);
}

void finite_domain_raw_amo_survives_forced_atom_simplification() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (not (= x a)))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.forced-atoms.rewrites") >= 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-choices") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.post-amo-clauses") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.deduped-amo-clauses") == 1);
}

void finite_domain_amo_uses_forced_disequalities_modulo_forced_equalities() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun z () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (assert (= y z))
    (assert (not (= x z)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.forced-atoms.disequalities") >= 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-amo-clauses") == 6);
  assert(result.statistics.get(":preprocessing.finite-domain.post-amo-clauses") == 9);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 12);
  assert(result.statistics.get(":preprocessing.finite-domain.deduped-amo-clauses") == 3);
}

void auto_preprocessing_uses_strong_finite_domain_for_multiple_assertions() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Auto;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.single-assertion") == 0);
  assert(result.statistics.get(":preprocessing.auto.strong") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 3);
  assert(result.statistics.get(":sat.phase.theory-negative") ==
         result.statistics.get(":euf.theory-atoms"));
  assert(result.statistics.get(":sat.phase.theory-negative") > 0);
}

void auto_preprocessing_emits_guarded_equality_definitions() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Auto;
  options.auto_finite_domain_equality_definitions = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (assert (not (= x y)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.strong") == 1);
  assert(result.statistics.get(":preprocessing.equality-definitions.candidate-pairs") >= 1);
  assert(result.statistics.get(":preprocessing.equality-definitions.accepted-pairs") >= 1);
  assert(result.statistics.get(":preprocessing.equality-definitions") >= 9);
}

void finite_domain_equality_definitions_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Auto;
  options.auto_finite_domain_equality_definitions = true;
  options.finite_domain_equality_definitions = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") >= 6);
  assert(result.statistics.get(":preprocessing.equality-definitions") == 0);
  assert(result.statistics.get(":preprocessing.equality-definitions.candidate-pairs") == 0);
}

void auto_preprocessing_keeps_equality_definitions_opt_in() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Auto;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") >= 6);
  assert(result.statistics.get(":preprocessing.equality-definitions") == 0);
  assert(result.statistics.get(":preprocessing.equality-definitions.candidate-pairs") == 0);
}

void default_symmetry_breaking_reports_covered_values_and_terms() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun z () A)
    (declare-fun P (A) Bool)
    (assert (distinct a b c))
    (assert (or (P a) (P b) (P c)))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (assert (or (= z a) (= z b) (= z c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 1);
  assert(result.statistics.get(":preprocessing.uf-symmetry.values") == 3);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 3);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 6);
  assert(result.statistics.get(":preprocessing.uf-symmetry.value-precedence-terms") == 3);
  assert(result.statistics.get(":preprocessing.uf-symmetry.value-precedence-constraints") == 6);
}

void moving_only_symmetry_terms_are_rejected_from_smtlib() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (distinct a b c))
    (assert (or (= (f a) a) (= (f a) b) (= (f a) c)))
    (assert (or (= (f b) a) (= (f b) b) (= (f b) c)))
    (assert (or (= (f c) a) (= (f c) b) (= (f c) c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.values") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.rejected-noninvariant") == 1);
}

void symmetry_ignores_preprocessing_residual_domains() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (not (= x a)))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= (f a) a) (= (f a) b) (= (f a) c)))
    (assert (= x c))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.forced-atoms.rewrites") >= 1);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 0);
}

void qg_shape_uses_fixed_terms_but_not_moving_table_terms_for_symmetry() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun op (A A) A)
    (declare-fun sk1 () A)
    (declare-fun sk2 () A)
    (assert (distinct a b c))
    (assert (or (= (op a a) a) (= (op a a) b) (= (op a a) c)))
    (assert (or (= (op b b) a) (= (op b b) b) (= (op b b) c)))
    (assert (or (= (op c c) a) (= (op c c) b) (= (op c c) c)))
    (assert (or (= sk1 a) (= sk1 b) (= sk1 c)))
    (assert (or (= sk2 a) (= sk2 b) (= sk2 c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 1);
  assert(result.statistics.get(":preprocessing.uf-symmetry.values") == 3);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 2);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 4);
  assert(result.statistics.get(":preprocessing.uf-symmetry.value-precedence-terms") == 2);
  assert(result.statistics.get(":preprocessing.uf-symmetry.value-precedence-constraints") == 4);
}

void no_symmetry_breaking_disables_strong_preset_symmetry_only() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  options.uf_symmetry_breaking = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun z () A)
    (declare-fun P (A) Bool)
    (assert (distinct a b c))
    (assert (or (P a) (P b) (P c)))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (assert (or (= z a) (= z b) (= z c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 9);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.values") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 0);
}

void theory_phase_option_can_leave_backend_phase_unset() {
  primo::smt::CheckSatOptions options;
  options.theory_phase = primo::smt::TheoryPhaseMode::None;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (= a b))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":sat.phase.theory-negative") == 0);
}

void theory_phase_auto_uses_polarity_on_balanced_finite_domain_shape() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  options.theory_phase = primo::smt::TheoryPhaseMode::Auto;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":sat.phase.auto-polarity") == 1);
  assert(result.statistics.get(":sat.phase.theory-negative") > 0);
  assert(result.statistics.get(":sat.phase.theory-negative") <
         result.statistics.get(":euf.theory-atoms"));
}

void theory_phase_auto_keeps_negative_phase_for_positive_dominated_shape() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  options.theory_phase = primo::smt::TheoryPhaseMode::Auto;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun d () A)
    (declare-fun e () A)
    (declare-fun f () A)
    (declare-fun g () A)
    (declare-fun h () A)
    (declare-fun i () A)
    (declare-fun x () A)
    (assert (distinct a b))
    (assert (or (= x a) (= x b)))
    (assert (= c a))
    (assert (= d a))
    (assert (= e a))
    (assert (= f a))
    (assert (= g a))
    (assert (= h a))
    (assert (= i a))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":sat.phase.auto-polarity") == 0);
  assert(result.statistics.get(":sat.phase.theory-negative") ==
         result.statistics.get(":euf.theory-atoms"));
}

void theory_phase_auto_uses_polarity_for_low_negative_positive_dominated_shape() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun a () A)\n";
  input << "(declare-fun b () A)\n";
  input << "(declare-fun x () A)\n";
  input << "(assert (distinct a b))\n";
  input << "(assert (or (= x a) (= x b)))\n";
  for (int i = 0; i < 110; ++i) {
    input << "(declare-fun c" << i << " () A)\n";
    input << "(assert (= c" << i << " a))\n";
  }
  input << "(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  options.theory_phase = primo::smt::TheoryPhaseMode::Auto;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":euf.pure-atoms.positive") >= 100);
  assert(result.statistics.get(":euf.pure-atoms.negative") <= 25);
  assert(result.statistics.get(":sat.phase.auto-polarity") == 1);
  assert(result.statistics.get(":sat.phase.theory-negative") <
         result.statistics.get(":euf.theory-atoms"));
}

void strong_preprocessing_enables_ground_congruence_clauses() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  options.ground_congruence = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (not (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":preprocessing.ground-congruence-clauses") >= 1);
}

void model_euf_propagation_learns_congruence_lemma() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Model;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (or p (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.rounds") == 1);
  assert(result.statistics.get(":euf.propagation.candidates") >= 2);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
  assert(result.statistics.get(":euf.propagation.reason-lits") >= 1);
}

void incremental_model_euf_propagation_uses_flat_lookup() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_engine = primo::smt::EufEngine::Incremental;
  options.euf_propagation = primo::smt::EufPropagationMode::Model;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (or p (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
  assert(result.statistics.get(":euf.incremental.lookup-hits") >= 1);
  assert(result.statistics.get(":euf.incremental.congruence-merges") >= 1);
}

void model_euf_propagation_handles_predicate_congruence() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Model;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p (A) Bool)
    (assert (= a b))
    (assert (p a))
    (assert (p b))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
}

void model_euf_propagation_respects_zero_budget() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Model;
  options.euf_propagation_budget.kind = primo::smt::AutoBudget::Kind::Fixed;
  options.euf_propagation_budget.value = 0;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (or p (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.rounds") == 1);
  assert(result.statistics.get(":euf.propagation.candidates") == 0);
  assert(result.statistics.get(":euf.propagation.delivered") == 0);
  assert(result.statistics.get(":euf.propagation.budget-cutoffs") == 1);
}

void online_euf_propagation_learns_congruence_during_search() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (or p (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.rounds") >= 1);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
}

void incremental_online_euf_propagation_uses_flat_lookup() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_engine = primo::smt::EufEngine::Incremental;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (or (= (f a) (f b)) (not (= (f a) (f b)))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
  assert(result.statistics.get(":euf.incremental.terms") >= 1);
  assert(result.statistics.get(":euf.incremental.flat-definitions") >= 1);
  assert(result.statistics.get(":euf.incremental.lookup-hits") >= 1);
  assert(result.statistics.get(":euf.incremental.congruence-merges") >= 1);
}

void online_euf_propagation_handles_transitive_reasons() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (= b c))
    (assert (or p (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
  assert(result.statistics.get(":euf.propagation.reason-lits") >= 2);
}

void online_euf_propagation_handles_predicate_congruence() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p (A) Bool)
    (declare-fun q () Bool)
    (assert (= a b))
    (assert (p a))
    (assert (or q (p b)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
}

void online_euf_propagation_keeps_conflicts_external() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":euf.theory-conflicts") >= 1);
}

void conflict_euf_callback_learns_external_conflict_without_propagation() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= (f a) (f c))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":euf.assignments") >= 3);
  assert(result.statistics.get(":euf.eq-assignments") >= 2);
  assert(result.statistics.get(":euf.diseq-assignments") >= 1);
  assert(result.statistics.get(":euf.theory-conflicts") >= 1);
  assert(result.statistics.get(":euf.propagation.rounds") == 0);
}

void theory_conflict_limit_stops_lazy_search_with_unknown() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.max_theory_conflicts = 1;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unknown);
  assert(result.message == "theory conflict limit reached");
  assert(result.statistics.get(":euf.theory-conflicts") == 1);
}

void theory_conflict_limit_stops_external_search_with_unknown() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  options.max_theory_conflicts = 1;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unknown);
  assert(result.message == "theory conflict limit reached");
  assert(result.statistics.get(":euf.theory-conflicts") == 1);
  assert(result.statistics.get(":euf.assignments") >= 3);
}

void online_euf_propagation_accepts_already_assigned_implications() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (= (f a) (f b)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
}

void online_euf_reason_cache_survives_backtracking() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  options.euf_propagation_interval = 32;
  options.euf_propagation_assign_threshold = 1.0;
  options.euf_propagation_delivery.kind = primo::smt::AutoBudget::Kind::Fixed;
  options.euf_propagation_delivery.value = 1000;
  const std::string input =
      read_text_file("../benchmarks/regression/qf_uf/online_reason_cache.smt2");
  const auto result = check_sat_smtlib(input, options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
}

void boolean_unit_simplification_handles_nested_rewrites() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (assert p)
    (assert (= (= p q) r))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.boolean-units.rewrites") >= 1);
}

}  // namespace

int main() {
  pure_boolean_sat();
  top_level_conjunction_does_not_allocate_auxiliary();
  top_level_or_uses_tseitin_support_clauses_by_default();
  pure_boolean_unsat();
  implication_is_clausified();
  implication_is_right_associative();
  xor_is_clausified();
  repeated_negation_reuses_the_child_literal();
  boolean_equality_is_propositional_equivalence();
  self_contradictory_boolean_equality_is_unsat();
  boolean_distinct_is_propositional_xor();
  reflexive_equality_is_sat();
  pure_euf_disequality_contradiction_is_unsat();
  default_euf_propagation_auto_uses_online_for_small_abstractions();
  default_euf_propagation_auto_uses_conflict_for_large_abstractions();
  default_euf_propagation_auto_uses_online_for_large_finite_domain_shapes();
  default_euf_propagation_auto_keeps_conflict_for_negative_heavy_finite_domain_shapes();
  chainable_equality_is_expanded();
  let_bound_terms_are_resolved();
  distinct_lowers_to_pairwise_disequalities();
  mixed_boolean_and_euf_can_be_sat();
  mixed_boolean_and_euf_conflict_is_unsat();
  predicate_applications_respect_congruence();
  annotated_assertions_are_solved();
  define_fun_macros_are_expanded();
  compound_bool_arguments_are_bound_before_euf_flattening();
  recursive_define_fun_is_rejected();
  boolean_ite_is_eliminated();
  term_ite_is_eliminated();
  true_boolean_constant_used_as_uf_argument_is_euf_term();
  false_boolean_constant_used_as_uf_argument_is_euf_term();
  equality_bridge_solves_diamond_chain();
  ground_unit_substitution_option_records_rewrites();
  guarded_ground_unit_substitution_rejects_tiny_rewrite();
  solved_constant_substitution_records_rewrites_by_default();
  solved_constant_substitution_can_be_disabled();
  auto_preprocessing_bridges_single_assertion_without_finite_domain_expansion();
  auto_preprocessing_skips_large_flat_assertion_batches();
  preprocessing_off_disables_basic_loop();
  preprocessing_extends_when_passes_keep_shrinking_terms();
  preprocessing_adaptive_extra_passes_can_be_disabled();
  positive_pure_theory_literal_is_filtered_from_euf_check();
  pure_theory_literal_filter_can_be_disabled();
  mixed_polarity_theory_literal_is_not_filtered();
  strong_preprocessing_enables_finite_domain_amo();
  finite_domain_raw_amo_survives_forced_atom_simplification();
  finite_domain_amo_uses_forced_disequalities_modulo_forced_equalities();
  auto_preprocessing_uses_strong_finite_domain_for_multiple_assertions();
  auto_preprocessing_emits_guarded_equality_definitions();
  finite_domain_equality_definitions_can_be_disabled();
  auto_preprocessing_keeps_equality_definitions_opt_in();
  default_symmetry_breaking_reports_covered_values_and_terms();
  moving_only_symmetry_terms_are_rejected_from_smtlib();
  symmetry_ignores_preprocessing_residual_domains();
  qg_shape_uses_fixed_terms_but_not_moving_table_terms_for_symmetry();
  no_symmetry_breaking_disables_strong_preset_symmetry_only();
  theory_phase_option_can_leave_backend_phase_unset();
  theory_phase_auto_uses_polarity_on_balanced_finite_domain_shape();
  theory_phase_auto_keeps_negative_phase_for_positive_dominated_shape();
  theory_phase_auto_uses_polarity_for_low_negative_positive_dominated_shape();
  strong_preprocessing_enables_ground_congruence_clauses();
  model_euf_propagation_learns_congruence_lemma();
  incremental_model_euf_propagation_uses_flat_lookup();
  model_euf_propagation_handles_predicate_congruence();
  model_euf_propagation_respects_zero_budget();
  online_euf_propagation_learns_congruence_during_search();
  incremental_online_euf_propagation_uses_flat_lookup();
  online_euf_propagation_handles_transitive_reasons();
  online_euf_propagation_handles_predicate_congruence();
  online_euf_propagation_keeps_conflicts_external();
  conflict_euf_callback_learns_external_conflict_without_propagation();
  theory_conflict_limit_stops_lazy_search_with_unknown();
  theory_conflict_limit_stops_external_search_with_unknown();
  online_euf_propagation_accepts_already_assigned_implications();
  online_euf_reason_cache_survives_backtracking();
  boolean_unit_simplification_handles_nested_rewrites();
  return 0;
}
