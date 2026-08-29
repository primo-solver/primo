#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/preprocessing_tactics.hpp"
#include "smt/solver_error.hpp"
#include "smt/solver_tactic.hpp"
#include "smt/tactic.hpp"
#include "util/statistics.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

struct Fixture {
  primo::core::SymbolTable symbols;
  primo::core::SymbolId p = symbols.declare_constant("p", symbols.bool_sort());
  primo::core::TermManager terms{symbols};
  primo::util::Statistics statistics;
  primo::smt::TacticContext context{terms, statistics};

  primo::core::TermId p_term() { return terms.mk_constant(p); }
};

void goal_truth_is_derived_from_assertions() {
  Fixture fixture;
  static_assert(std::is_same_v<decltype(std::declval<const primo::smt::Goal&>().assertions()),
                               const std::vector<primo::core::TermId>&>);
  check(primo::smt::truth(primo::smt::Goal{}, fixture.context) == primo::smt::GoalTruth::True);
  check(primo::smt::truth(primo::smt::Goal{{fixture.terms.mk_bool(false)}}, fixture.context) ==
        primo::smt::GoalTruth::False);
  check(primo::smt::truth(primo::smt::Goal{{fixture.p_term(), fixture.terms.mk_bool(false)}},
                          fixture.context) == primo::smt::GoalTruth::False);
  check(primo::smt::truth(primo::smt::Goal{{fixture.p_term()}}, fixture.context) ==
        primo::smt::GoalTruth::Inconclusive);

  const primo::smt::Goal original{{fixture.p_term()}};
  const primo::smt::Goal diagnosed =
      original.with_diagnostic(primo::smt::InconclusiveReason::ResourceLimit, "budget");
  check(original.diagnostic().empty());
  check(diagnosed.reason() == primo::smt::InconclusiveReason::ResourceLimit);
  check(diagnosed.assertions() == original.assertions());
}

void reconstruction_records_are_goal_local_immutable_values() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  primo::smt::ModelReconstruction reconstruction;
  reconstruction.model_assignments.push_back(
      primo::smt::TheoryModelAssignment{.term = p, .value = true});
  reconstruction.forced_equalities.emplace_back(p, p);

  const primo::smt::Goal original{{p}};
  const primo::smt::Goal transformed = original.with_reconstruction(reconstruction);
  reconstruction.model_assignments.clear();

  check(original.reconstruction().model_assignments.empty());
  check(transformed.reconstruction().model_assignments.size() == 1);
  check(transformed.reconstruction().model_assignments.front().term == p);
  check(transformed.reconstruction().forced_equalities ==
        std::vector<std::pair<primo::core::TermId, primo::core::TermId>>{{p, p}});
  check(transformed.with_assertions({}).reconstruction() == transformed.reconstruction());
}

void anyof_has_disjunctive_semantics() {
  Fixture fixture;
  const primo::smt::Goal unresolved{{fixture.p_term()}};
  const primo::smt::Goal falsified{{fixture.terms.mk_bool(false)}};
  const primo::smt::Goal satisfied;

  check(primo::smt::truth(primo::smt::AnyOf({falsified, unresolved}), fixture.context) ==
        primo::smt::GoalTruth::Inconclusive);
  check(primo::smt::truth(primo::smt::AnyOf({falsified, satisfied}), fixture.context) ==
        primo::smt::GoalTruth::True);
  check(primo::smt::truth(primo::smt::AnyOf({falsified, falsified}), fixture.context) ==
        primo::smt::GoalTruth::False);
}

void empty_anyof_is_rejected() {
  bool threw = false;
  try {
    static_cast<void>(primo::smt::AnyOf({}));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  check(threw);
}

void non_boolean_goal_assertion_is_a_contract_error() {
  Fixture fixture;
  bool threw = false;
  try {
    static_cast<void>(
        primo::smt::truth(primo::smt::Goal{{fixture.terms.mk_rational(0)}}, fixture.context));
  } catch (const primo::smt::TacticError&) {
    threw = true;
  }
  check(threw);
}

void sequence_and_repeat_stop_at_a_fixed_point() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  std::size_t applications = 0;
  const primo::smt::Tactic remove_one = primo::smt::make_goal_tactic(
      "remove-one", [&applications](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++applications;
        std::vector<primo::core::TermId> assertions = goal.assertions();
        assertions.pop_back();
        return primo::smt::AnyOf::single(goal.with_assertions(std::move(assertions)));
      });
  const primo::smt::Tactic repeated = primo::smt::repeat("repeat-remove", remove_one, 8);
  const primo::smt::AnyOf result =
      repeated(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p, p, p}}));
  check(primo::smt::truth(result, fixture.context) == primo::smt::GoalTruth::True);
  check(applications == 3);
  check(fixture.statistics.get(":tactic.remove-one.invocations") == 3);
}

void conditional_uses_a_probe() {
  Fixture fixture;
  const primo::smt::Tactic make_true = primo::smt::make_goal_tactic(
      "make-true", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_assertions({}));
      });
  const primo::smt::Tactic choose = primo::smt::conditional(
      "choose",
      [](const primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return goal.assertions().size() == 1;
      },
      make_true, primo::smt::skip());
  const primo::smt::AnyOf result =
      choose(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(primo::smt::truth(result, fixture.context) == primo::smt::GoalTruth::True);
}

void sequential_scheduler_tries_each_arm_from_the_original_goal() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::smt::Tactic inconclusive = primo::smt::make_goal_tactic(
      "inconclusive", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_diagnostic("budget"));
      });
  const primo::smt::Tactic solve = primo::smt::make_goal_tactic(
      "solve", [p](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        check(goal.assertions() == std::vector<primo::core::TermId>{p});
        check(goal.diagnostic().empty());
        return primo::smt::AnyOf::single(goal.with_assertions({}));
      });
  const auto scheduler = std::make_shared<primo::smt::SequentialTacticScheduler>();
  const primo::smt::Tactic portfolio =
      primo::smt::first_terminal("portfolio", scheduler, {inconclusive, solve});
  const primo::smt::AnyOf result =
      portfolio(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(primo::smt::truth(result, fixture.context) == primo::smt::GoalTruth::True);
}

void cancellation_stops_before_running_a_tactic() {
  Fixture fixture;
  std::size_t applications = 0;
  primo::smt::TacticContext cancelled_context(fixture.terms, fixture.statistics,
                                              [] { return true; });
  const primo::smt::Tactic tactic = primo::smt::make_goal_tactic(
      "must-not-run", [&applications](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++applications;
        return primo::smt::AnyOf::single(goal);
      });

  const primo::smt::AnyOf result =
      tactic(cancelled_context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(applications == 0);
  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::Cancelled);
  check(result.alternatives().front().diagnostic() == "cancelled");
}

void check_sat_honors_an_existing_cancellation_request() {
  primo::smt::CheckSatOptions options;
  options.cancelled = [] { return true; };
  const auto result = primo::smt::check_sat_smtlib("(check-sat)", options);
  check(result.result == primo::smt::CheckSatResult::Unknown);
  check(result.message == "cancelled");
}

void solver_adapter_types_expected_inconclusive_results() {
  Fixture fixture;
  const primo::smt::Tactic unsupported = primo::smt::make_solver_tactic(
      "unsupported", fixture.terms, [](const primo::smt::Goal&) -> primo::smt::CheckSatOutcome {
        throw primo::smt::UnsupportedInput("outside fragment");
      });
  const primo::smt::AnyOf result =
      unsupported(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));

  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::Unsupported);
  const primo::smt::CheckSatOutcome outcome =
      primo::smt::outcome_from_solver_result(result, fixture.context);
  check(outcome.result == primo::smt::CheckSatResult::Unsupported);
  check(outcome.message == "outside fragment");
}

void solver_adapter_does_not_hide_serious_failures() {
  Fixture fixture;
  const primo::smt::Tactic broken = primo::smt::make_solver_tactic(
      "broken", fixture.terms, [](const primo::smt::Goal&) -> primo::smt::CheckSatOutcome {
        throw std::runtime_error("backend invariant");
      });
  bool threw = false;
  try {
    static_cast<void>(
        broken(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}})));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw);
}

void preprocessing_round_transforms_each_alternative_independently() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::core::TermId not_p = fixture.terms.mk_not(p);
  primo::smt::ModelReconstruction true_reconstruction;
  true_reconstruction.model_assignments.push_back(
      primo::smt::TheoryModelAssignment{.term = p, .value = true});
  primo::smt::ModelReconstruction false_reconstruction;
  false_reconstruction.model_assignments.push_back(
      primo::smt::TheoryModelAssignment{.term = p, .value = false});
  const primo::smt::Tactic round =
      primo::smt::make_preprocessing_round(primo::smt::PreprocessingRoundOptions{});

  const primo::smt::AnyOf result = round(
      fixture.context,
      primo::smt::AnyOf({primo::smt::Goal{{p}}.with_reconstruction(true_reconstruction),
                         primo::smt::Goal{{not_p}}.with_reconstruction(false_reconstruction)}));
  check(result.size() == 2);
  check(result.alternatives()[0].reconstruction().model_assignments.size() == 1);
  check(result.alternatives()[1].reconstruction().model_assignments.size() == 1);
  check(result.alternatives()[0].reconstruction().model_assignments.front().value);
  check(!result.alternatives()[1].reconstruction().model_assignments.front().value);
  check(fixture.statistics.get(":tactic.preprocessing-round.invocations") == 1);
  check(fixture.statistics.get(":tactic.boolean-unit-propagation.invocations") == 1);
  check(fixture.statistics.get(":tactic.forced-atom-propagation.invocations") == 1);
  check(fixture.statistics.get(":tactic.unit-equality-propagation.invocations") == 1);
}

void solver_dispatch_uses_formula_tactics() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.solved_constant_substitution = false;

  const auto boolean_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert p)
    (check-sat)
  )",
                                                           options);
  check(boolean_result.result == primo::smt::CheckSatResult::Sat);
  check(boolean_result.statistics.get(":tactic.solver-dispatch.invocations") == 1);
  check(boolean_result.statistics.get(":tactic.solve-boolean.invocations") == 1);

  const auto euf_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) U)
    (assert (= (f a) (f b)))
    (check-sat)
  )",
                                                       options);
  check(euf_result.result == primo::smt::CheckSatResult::Sat);
  check(euf_result.statistics.get(":tactic.solve-qf-uf.invocations") == 1);

  const auto lra_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (< x 0))
    (check-sat)
  )",
                                                       options);
  check(lra_result.result == primo::smt::CheckSatResult::Sat);
  check(lra_result.statistics.get(":tactic.solve-qf-lra.invocations") == 1);

  const auto mixed_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun f (Real) Real)
    (assert (< x 0))
    (assert (= (f x) x))
    (check-sat)
  )",
                                                         options);
  check(mixed_result.result == primo::smt::CheckSatResult::Sat);
  check(mixed_result.statistics.get(":tactic.solve-qf-uflra.invocations") == 1);

  const auto lia_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (< x 0))
    (check-sat)
  )",
                                                       options);
  check(lia_result.result == primo::smt::CheckSatResult::Sat);
  check(lia_result.statistics.get(":tactic.solve-qf-lia.invocations") == 1);
}

}  // namespace

int main() {
  goal_truth_is_derived_from_assertions();
  reconstruction_records_are_goal_local_immutable_values();
  anyof_has_disjunctive_semantics();
  empty_anyof_is_rejected();
  non_boolean_goal_assertion_is_a_contract_error();
  sequence_and_repeat_stop_at_a_fixed_point();
  conditional_uses_a_probe();
  sequential_scheduler_tries_each_arm_from_the_original_goal();
  cancellation_stops_before_running_a_tactic();
  check_sat_honors_an_existing_cancellation_request();
  solver_adapter_types_expected_inconclusive_results();
  solver_adapter_does_not_hide_serious_failures();
  preprocessing_round_transforms_each_alternative_independently();
  solver_dispatch_uses_formula_tactics();
}
