#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>

#include "smt/check_sat.hpp"
#include "smt/inconclusive_reason.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatOptions;
using primo::smt::CheckSatOutcome;
using primo::smt::CheckSatResult;
using primo::smt::InconclusiveReason;

namespace {

const std::string kPrelude = R"(
  (set-logic UF)
  (declare-sort U 0)
  (declare-fun a () U)
  (declare-fun b () U)
  (declare-fun f (U) U)
  (declare-fun p (U) Bool)
  (declare-fun q (U) Bool)
  (declare-fun r (U U) Bool)
)";

CheckSatOutcome solve(const std::string& commands, CheckSatOptions options = {}) {
  return check_sat_smtlib(kPrelude + commands + "(check-sat)", std::move(options));
}

void single_instances_refute() {
  assert(solve("(assert (forall ((x U)) (p x))) (assert (not (p a)))").result ==
         CheckSatResult::Unsat);
  // Two variables at once.
  assert(solve("(assert (forall ((x U) (y U)) (r x y))) (assert (not (r a b)))").result ==
         CheckSatResult::Unsat);
}

void skolemization_covers_both_polarities() {
  // A true exists is Skolemized; the universal is then instantiated with the constant.
  assert(solve("(assert (exists ((x U)) (p x))) (assert (forall ((y U)) (not (p y))))").result ==
         CheckSatResult::Unsat);
  // A false forall behaves existentially.
  assert(solve("(assert (not (forall ((x U)) (p x)))) (assert (forall ((y U)) (p y)))").result ==
         CheckSatResult::Unsat);
  // A false exists behaves universally.
  assert(solve("(assert (not (exists ((x U)) (p x)))) (assert (p a))").result ==
         CheckSatResult::Unsat);
}

void nested_quantifiers_become_atoms_of_instances() {
  // x := a yields the closed atom (exists y. r(a, y)), Skolemized to r(a, c);
  // the constant c then enters the pool for z.
  assert(solve("(assert (forall ((x U)) (exists ((y U)) (r x y)))) "
               "(assert (forall ((z U)) (not (r a z))))")
             .result == CheckSatResult::Unsat);
}

void terms_from_instances_are_used_later() {
  // Theorem 1: f(a) exists only once the instance x := a has been added.
  assert(
      solve("(assert (forall ((x U)) (p (f x)))) (assert (forall ((y U)) (not (p y))))").result ==
      CheckSatResult::Unsat);
}

void paper_example_two_refutes_on_the_minimal_term() {
  // Reynolds, Barbosa, Fontaine 2018, Example 2: E = {not P(a), R(b), S(c)}.
  const std::string commands = R"(
    (declare-fun c () U)
    (declare-fun s (U) Bool)
    (assert (not (p a)))
    (assert (q b))
    (assert (s c))
    (assert (forall ((x U)) (or (q x) (s x))))
    (assert (forall ((x U)) (or (not (q x)) (p x))))
    (assert (forall ((x U)) (or (not (s x)) (p x))))
  )";
  assert(solve(commands).result == CheckSatResult::Unsat);
}

void boolean_structure_in_bodies_is_prepared() {
  // Boolean-sorted variables range over true and false.
  assert(solve("(declare-fun g (Bool) Bool) (assert (forall ((v Bool)) (g v))) "
               "(assert (not (g false)))")
             .result == CheckSatResult::Unsat);
  // An ite in the body is lowered when the instance becomes a lemma.
  assert(solve("(assert (forall ((x U)) (= (ite (p x) (f x) x) a))) (assert (p a)) "
               "(assert (not (= (f a) a)))")
             .result == CheckSatResult::Unsat);
  // A compound Boolean argument in the body is named when instantiated, so EUF
  // relates h(p(a)) to h(true) once p(a) holds.
  assert(solve("(declare-fun h (Bool) U) (assert (forall ((x U)) (= (h (p x)) a))) "
               "(assert (p a)) (assert (not (= (h true) a)))")
             .result == CheckSatResult::Unsat);
}

void answers_short_of_refutation() {
  // Covered by Skolemization alone: a sound sat.
  assert(solve("(assert (exists ((x U)) (p x)))").result == CheckSatResult::Sat);
  // A universal with nothing to refute: no entailment check, so unknown.
  const CheckSatOutcome universal = solve("(assert (forall ((x U)) (p x)))");
  assert(universal.result == CheckSatResult::Unknown);
  assert(universal.statistics.get(":quant.exhausted") >= 1);
  // Paper Example 3 shape: satisfiable, so enumeration ends in unknown.
  assert(solve("(declare-fun c () U) (assert (distinct a b c)) (assert (forall ((x U)) (p x)))")
             .result == CheckSatResult::Unknown);
}

void budgets_models_and_logics() {
  CheckSatOptions tight;
  tight.quantifier_max_instances = 1;
  const CheckSatOutcome limited =
      solve("(assert (forall ((x U)) (p (f x)))) (assert (forall ((y U)) (not (p y))))", tight);
  assert(limited.result == CheckSatResult::Unknown);
  assert(limited.reason == InconclusiveReason::ResourceLimit);

  CheckSatOptions cancelled;
  cancelled.cancelled = [] { return true; };
  assert(solve("(assert (forall ((x U)) (p x)))", cancelled).result == CheckSatResult::Unknown);

  const CheckSatOutcome models =
      check_sat_smtlib("(set-option :produce-models true)" + kPrelude +
                       "(assert (forall ((x U)) (p x))) (check-sat) (get-model)");
  assert(models.result == CheckSatResult::Unsupported);

  assert(check_sat_smtlib("(set-logic QF_UF) (declare-sort U 0) (declare-fun p (U) Bool) "
                          "(assert (forall ((x U)) (p x))) (check-sat)")
             .result == CheckSatResult::Unsupported);
}

void instances_are_normalized_and_deduplicated() {
  // (a, b) and (b, a) instantiate to (or (p a) (p b)) and (or (p b) (p a)).
  const std::string symmetric =
      "(assert (forall ((x U) (y U)) (or (p x) (p y)))) "
      "(assert (not (p a))) (assert (not (p b)))";
  const CheckSatOutcome simplified = solve(symmetric);
  assert(simplified.result == CheckSatResult::Unsat);
  assert(simplified.statistics.get(":quant.instances.simplified-duplicates") >= 1);

  CheckSatOptions plain;
  plain.quantifier_instance_simplification = false;
  const CheckSatOutcome unsimplified = solve(symmetric, plain);
  assert(unsimplified.result == CheckSatResult::Unsat);
  assert(unsimplified.statistics.get(":quant.instances.simplified-duplicates") == 0);

  // x := y makes the instance true outright; it must not become a lemma.
  const CheckSatOutcome trivial = solve(
      "(assert (forall ((x U) (y U)) (or (= x y) (r x y)))) (assert (not (r a b))) (assert (not (= "
      "a b)))");
  assert(trivial.result == CheckSatResult::Unsat);
  assert(trivial.statistics.get(":quant.instances.trivial") >= 1);
}

// benchmarks/regressions/uf_quantified_pure_filter_growth.smt2: the pure-theory
// filter assumes a final clause set, but instance lemmas add atom occurrences,
// so the quantified route must not filter. With the filter this was unknown.
void pure_theory_filter_stays_off_while_lemmas_grow() {
  const std::string input = R"(
    (set-logic UF)
    (declare-sort A 0) (declare-sort B 0) (declare-sort C 0) (declare-sort D 0)
    (declare-sort E 0) (declare-sort F 0) (declare-sort G 0)
    (declare-fun a () A) (declare-fun h (C B) D) (declare-fun c () C)
    (declare-fun k (C E) D) (declare-fun m (D) C) (declare-fun n (B) D)
    (declare-fun b () B) (declare-fun r (F B) B) (declare-fun s (G B) F)
    (declare-fun g () G) (declare-fun d () B) (declare-fun u (B B) E)
    (declare-fun q (B B) A) (declare-fun w (E E) E) (declare-fun x (B E) E)
    (declare-fun e () E) (declare-fun t (B B) E)
    (assert (forall ((y B)) (= (h c b) (k (m (h (m (n b)) (r (s g y) b))) (t b b)))))
    (assert (forall ((y B) (v B)) (or (not (= a (q d v))) (= (u d v) (w (x y e) (t d v))))))
    (assert (and (= (q d b) a) (not (= (k c (u d b)) (k c (w (x b e) (t d b)))))))
    (check-sat)
  )";
  for (const bool simplify : {true, false}) {
    for (const auto filter :
         {primo::smt::PureTheoryFilterMode::Auto, primo::smt::PureTheoryFilterMode::On}) {
      CheckSatOptions options;
      options.quantifier_instance_simplification = simplify;
      options.pure_theory_filter = filter;
      options.quantifier_max_instances = 5000;
      assert(check_sat_smtlib(input, options).result == CheckSatResult::Unsat);
    }
  }
}

// The instance budget counts distinct instances, not tuples tried: y does not
// occur in the body, so 16 tuples over four constants give only four distinct
// instances. Counting tuples, a budget of five ran out (resource-limit) before
// enumeration was exhausted; counting instances, it answers plain unknown.
void duplicate_instances_do_not_spend_the_budget() {
  for (const bool simplify : {true, false}) {
    CheckSatOptions options;
    options.quantifier_instance_simplification = simplify;
    options.quantifier_max_instances = 5;
    const CheckSatOutcome outcome = solve(
        "(declare-fun c () U) (declare-fun d () U) (assert (distinct a b c d)) "
        "(assert (forall ((x U) (y U)) (p x)))",
        options);
    assert(outcome.result == CheckSatResult::Unknown);
    assert(outcome.reason == InconclusiveReason::Unknown);
    assert(outcome.statistics.get(":quant.instances") > 5);
  }
}

// benchmarks/regressions/uf_quantified_lemmas_before_complete_assignment.smt2:
// with the quantifier component consulted mid-search, its queued lemmas muted
// EUF before any decision-dependent conflict and this stayed unknown.
void instances_wait_for_complete_assignments() {
  const std::string input = R"(
    (set-logic UF)
    (declare-sort Loc 0) (declare-sort Set 0) (declare-sort Fld 0)
    (declare-fun null () Loc) (declare-fun ep (Fld Set Loc) Loc)
    (declare-fun btwn (Fld Loc Loc Loc) Bool) (declare-fun in (Loc Set) Bool)
    (declare-fun a () Loc) (declare-fun next () Fld) (declare-fun fp () Set)
    (assert (forall ((v Loc)) (or false (= v null) (not (btwn next null v null)))))
    (assert (not (= a null)))
    (assert (forall ((s Set) (f Fld) (x Loc) (y Loc)) (or false (not (in y fp)) (in (ep next fp x) fp))))
    (assert (forall ((f Fld) (u Loc) (x Loc) (y Loc) (z Loc))
      (or false (not (btwn next x y z)) (and (btwn next x y x) (btwn next z z y)))))
    (assert (forall ((f Fld) (x Loc) (y Loc) (z Loc)) (or false false (btwn next x y x) (btwn f x z z))))
    (check-sat)
  )";
  for (const bool simplify : {true, false}) {
    CheckSatOptions options;
    options.quantifier_instance_simplification = simplify;
    const CheckSatOutcome outcome = check_sat_smtlib(input, options);
    assert(outcome.result == CheckSatResult::Unsat);
    // 302 instances with the fix. Consulted mid-search, the component kept
    // queuing instances until the whole 100,000-instance budget was spent
    // before EUF could refute; instance counts are deterministic, time is not.
    assert(outcome.statistics.get(":quant.instances") < 2000);
  }
}

// Every tuple order is complete for refutation on small inputs: the paper's
// Example 2, a Theorem 1 shape needing a term from an instance, and a
// two-variable symmetric instance.
void every_tuple_order_refutes() {
  using primo::smt::QuantifierTupleOrder;
  const std::string example_two = R"(
    (declare-fun c () U) (declare-fun s (U) Bool)
    (assert (not (p a))) (assert (q b)) (assert (s c))
    (assert (forall ((x U)) (or (q x) (s x))))
    (assert (forall ((x U)) (or (not (q x)) (p x))))
    (assert (forall ((x U)) (or (not (s x)) (p x))))
  )";
  const std::string theorem_one =
      "(assert (forall ((x U)) (p (f x)))) (assert (forall ((y U)) (not (p y))))";
  const std::string two_variables =
      "(assert (forall ((x U) (y U)) (or (r x y) (r y x)))) (assert (not (r a b))) "
      "(assert (not (r b a)))";
  for (const auto order :
       {QuantifierTupleOrder::MaxDigit, QuantifierTupleOrder::MaxDigitColex,
        QuantifierTupleOrder::SumOfDigits, QuantifierTupleOrder::Leximax,
        QuantifierTupleOrder::IterativeDeepening, QuantifierTupleOrder::RandomWalk}) {
    for (const bool simplify : {true, false}) {
      CheckSatOptions options;
      options.quantifier_tuple_order = order;
      options.quantifier_instance_simplification = simplify;
      for (const auto& problem : {example_two, theorem_one, two_variables}) {
        assert(solve(problem, options).result == CheckSatResult::Unsat);
      }
    }
  }
}

}  // namespace

int main() {
  single_instances_refute();
  skolemization_covers_both_polarities();
  nested_quantifiers_become_atoms_of_instances();
  terms_from_instances_are_used_later();
  paper_example_two_refutes_on_the_minimal_term();
  boolean_structure_in_bodies_is_prepared();
  answers_short_of_refutation();
  budgets_models_and_logics();
  instances_are_normalized_and_deduplicated();
  pure_theory_filter_stays_off_while_lemmas_grow();
  instances_wait_for_complete_assignments();
  duplicate_instances_do_not_spend_the_budget();
  every_tuple_order_refutes();
  return 0;
}
