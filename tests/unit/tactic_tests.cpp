#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/preprocessing.hpp"
#include "smt/preprocessing_strategy.hpp"
#include "smt/preprocessing_tactics.hpp"
#include "smt/solver_answer.hpp"
#include "smt/solver_error.hpp"
#include "smt/solver_tactic.hpp"
#include "smt/strategy.hpp"
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

struct IdentityStrategyParameters {
  std::string alpha;
  std::string zeta;

  std::vector<primo::smt::StrategyParameter> strategy_parameters() const {
    return {{"zeta", zeta}, {"alpha", alpha}};
  }
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
  reconstruction.conversions =
      reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = true, .filtered = false, .directed_substitution = false}}});
  reconstruction.forced_equalities.emplace_back(p, p);

  const primo::smt::Goal original{{p}};
  const primo::smt::Goal transformed = original.with_reconstruction(reconstruction);
  reconstruction.conversions = {};

  check(original.reconstruction().conversions.empty());
  const auto assignments =
      transformed.reconstruction().conversions.theory_assignments(fixture.terms);
  check(assignments.size() == 1);
  check(assignments.front().term == p);
  check(transformed.reconstruction().forced_equalities ==
        std::vector<std::pair<primo::core::TermId, primo::core::TermId>>{{p, p}});
  check(transformed.with_assertions({}).reconstruction() == transformed.reconstruction());
}

void goal_updates_share_untouched_large_payloads() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  primo::smt::ModelReconstruction reconstruction;
  constexpr std::size_t kLargePayloadSize = 4096;
  primo::smt::BooleanTheoryAssignmentConversion assignments;
  assignments.assignments.reserve(kLargePayloadSize);
  for (std::size_t index = 0; index < kLargePayloadSize; ++index) {
    assignments.assignments.push_back(primo::smt::TheoryModelAssignment{
        .term = p, .value = index % 2 == 0, .filtered = false, .directed_substitution = false});
  }
  reconstruction.conversions = reconstruction.conversions.appended(std::move(assignments));
  const std::string model(kLargePayloadSize, 'm');
  const primo::smt::Goal original =
      primo::smt::Goal{{p}}.with_reconstruction(reconstruction).with_model(model);

  const primo::smt::Goal assertions_changed = original.with_assertions({p, p});
  check(&assertions_changed.reconstruction() == &original.reconstruction());
  check(&assertions_changed.model() == &original.model());

  const primo::smt::Goal diagnostic_changed = original.with_diagnostic("diagnostic");
  check(&diagnostic_changed.assertions() == &original.assertions());
  check(&diagnostic_changed.reconstruction() == &original.reconstruction());
  check(&diagnostic_changed.model() == &original.model());

  reconstruction.conversions =
      reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = true, .filtered = false, .directed_substitution = false}}});
  const primo::smt::Goal reconstruction_changed =
      original.with_reconstruction(std::move(reconstruction));
  check(&reconstruction_changed.assertions() == &original.assertions());
  check(&reconstruction_changed.model() == &original.model());
  check(reconstruction_changed.reconstruction().conversions.size() == 2);
  check(original.reconstruction().conversions.size() == 1);
}

void conversion_chain_replays_typed_steps_in_reverse_execution_order() {
  primo::smt::ModelConversionChain conversions;
  conversions = conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{});
  conversions = conversions.appended(primo::smt::DirectedEqualitySubstitutionConversion{});
  conversions = conversions.appended(primo::smt::MonotoneEliminationConversion{});

  int value = 0;
  conversions.replay_reverse([&value](const primo::smt::ModelConversionStep& step) {
    std::visit(
        [&value](const auto& conversion) {
          using Conversion = std::decay_t<decltype(conversion)>;
          if constexpr (std::is_same_v<Conversion, primo::smt::MonotoneEliminationConversion>) {
            value = 4;
          } else if constexpr (std::is_same_v<Conversion,
                                              primo::smt::DirectedEqualitySubstitutionConversion>) {
            value += 3;
          } else if constexpr (std::is_same_v<Conversion,
                                              primo::smt::BooleanTheoryAssignmentConversion>) {
            value *= 2;
          }
        },
        step);
  });
  check(value == 14);
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

void repeat_preserves_equivalent_output_and_observes_terminal_changes() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::smt::Tactic diagnose = primo::smt::make_goal_tactic(
      "diagnose", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_diagnostic("no-progress"));
      });
  const primo::smt::Tactic assertion_fixed_point =
      primo::smt::repeat("assertion-fixed-point", diagnose, 4, {},
                         [](const primo::smt::TacticContext&, const primo::smt::AnyOf& before,
                            const primo::smt::AnyOf& after) {
                           return before.alternatives().front().assertions() ==
                                  after.alternatives().front().assertions();
                         });
  const primo::smt::AnyOf diagnosed =
      assertion_fixed_point(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}}));
  check(diagnosed.alternatives().front().diagnostic() == "no-progress");
  check(fixture.statistics.get(":tactic.diagnose.invocations") == 1);

  std::size_t observed_changes = 0;
  const primo::smt::Tactic make_true = primo::smt::make_goal_tactic(
      "terminal-change", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_assertions({}));
      });
  const primo::smt::Tactic observe_terminal = primo::smt::repeat(
      "observe-terminal", make_true, 4,
      [&observed_changes](const primo::smt::TacticContext&, const primo::smt::AnyOf&,
                          const primo::smt::AnyOf&, std::size_t) {
        ++observed_changes;
        return true;
      });
  const primo::smt::AnyOf terminal =
      observe_terminal(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}}));
  check(primo::smt::truth(terminal, fixture.context) == primo::smt::GoalTruth::True);
  check(observed_changes == 1);
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

void strategy_is_immutable_canonical_and_fingerprinted() {
  Fixture fixture;
  const primo::smt::Tactic identity = primo::smt::make_goal_tactic(
      "identity", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal);
      });
  IdentityStrategyParameters parameters{.alpha = "1", .zeta = "2"};
  const primo::smt::Strategy leaf = primo::smt::Strategy::leaf("identity", parameters, identity);
  parameters.zeta = "changed";
  const primo::smt::Strategy strategy = primo::smt::Strategy::sequence(
      {leaf,
       primo::smt::Strategy::conditional(primo::smt::at_most(primo::smt::assertion_count(), 1.0),
                                         primo::smt::Strategy::skip(), leaf)});

  check(leaf.canonical() == "(leaf \"identity\" (\"alpha\" \"1\") (\"zeta\" \"2\"))");
  check(strategy.canonical() ==
        "(sequence (leaf \"identity\" (\"alpha\" \"1\") (\"zeta\" \"2\")) "
        "(conditional \"(<= assertion-count 1)\" skip (leaf \"identity\" "
        "(\"alpha\" \"1\") (\"zeta\" \"2\"))))");
  check(strategy.fingerprint() ==
        primo::smt::Strategy::sequence(
            {leaf, primo::smt::Strategy::conditional(
                       primo::smt::at_most(primo::smt::assertion_count(), 1.0),
                       primo::smt::Strategy::skip(), leaf)})
            .fingerprint());
  check(primo::smt::compile(strategy)(
            fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}))
            .size() == 1);
}

void strategy_probe_observes_the_transformed_goal_and_repeat_is_bounded() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  std::size_t repeats = 0;
  const primo::smt::Tactic duplicate = primo::smt::make_goal_tactic(
      "duplicate", [p](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_assertions({p, p}));
      });
  const primo::smt::Tactic count = primo::smt::make_goal_tactic(
      "count", [&repeats](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++repeats;
        return primo::smt::AnyOf::single(goal.with_diagnostic(std::to_string(repeats)));
      });
  const primo::smt::Strategy strategy = primo::smt::Strategy::sequence(
      {primo::smt::Strategy::leaf("duplicate", primo::smt::NoStrategyParameters{}, duplicate),
       primo::smt::Strategy::conditional(
           primo::smt::at_most(primo::smt::assertion_count(), 1.0), primo::smt::Strategy::skip(),
           primo::smt::Strategy::repeat(
               primo::smt::Strategy::leaf("count", primo::smt::NoStrategyParameters{}, count),
               3))});
  const primo::smt::AnyOf result = primo::smt::compile(strategy)(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}}));
  check(result.alternatives().front().assertions().size() == 2);
  // Structural equality includes the diagnostic, so all three bounded
  // iterations execute and the transformed assertion count drives the branch.
  check(repeats == 3);
}

void try_for_uses_context_deadlines_without_mutating_the_strategy() {
  Fixture fixture;
  std::size_t applications = 0;
  const primo::smt::Tactic must_not_run = primo::smt::make_goal_tactic(
      "must-not-run", [&applications](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++applications;
        return primo::smt::AnyOf::single(goal);
      });
  const primo::smt::Strategy strategy = primo::smt::Strategy::try_for(
      primo::smt::Strategy::leaf("must-not-run", primo::smt::NoStrategyParameters{}, must_not_run),
      std::chrono::milliseconds(0));
  const std::string fingerprint = strategy.fingerprint();
  const primo::smt::AnyOf result = primo::smt::compile(strategy)(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(applications == 0);
  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::ResourceLimit);
  check(strategy.fingerprint() == fingerprint);
  check(!fixture.context.deadline().has_value());
}

void solver_leaf_handles_disjunctive_goals_in_order() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::core::TermId not_p = fixture.terms.mk_not(p);
  std::size_t calls = 0;
  const primo::smt::Tactic solver = primo::smt::make_solver_tactic(
      "alternative-solver", fixture.terms,
      [p, &calls](const primo::smt::Goal& goal) -> primo::smt::SolverAnswer {
        ++calls;
        if (goal.assertions().front() == p) {
          return {.status = primo::smt::SolverStatus::Unsat,
                  .reason = primo::smt::InconclusiveReason::None,
                  .diagnostic = {},
                  .model = {}};
        }
        return {.status = primo::smt::SolverStatus::Sat,
                .reason = primo::smt::InconclusiveReason::None,
                .diagnostic = {},
                .model = "winning-model"};
      });
  const primo::smt::AnyOf sat = solver(
      fixture.context, primo::smt::AnyOf({primo::smt::Goal{{p}}, primo::smt::Goal{{not_p}}}));
  check(calls == 2);
  check(primo::smt::truth(sat, fixture.context) == primo::smt::GoalTruth::True);
  check(sat.alternatives().front().model() == std::optional<std::string>{"winning-model"});

  const primo::smt::AnyOf unsat =
      solver(fixture.context, primo::smt::AnyOf({primo::smt::Goal{{p}}, primo::smt::Goal{{p}}}));
  check(unsat.size() == 1);
  check(primo::smt::truth(unsat, fixture.context) == primo::smt::GoalTruth::False);
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

void check_sat_preserves_cancellation_observed_by_preprocessing() {
  for (const std::string_view input :
       {std::string_view{"(check-sat)"}, std::string_view{"(assert false) (check-sat)"}}) {
    std::size_t cancellation_probes = 0;
    primo::smt::CheckSatOptions options;
    options.cancelled = [&cancellation_probes] { return cancellation_probes++ > 0; };
    const primo::smt::CheckSatOutcome result = primo::smt::check_sat_smtlib(input, options);
    check(result.result == primo::smt::CheckSatResult::Unknown);
    check(result.message == "cancelled");
  }
}

void solver_adapter_types_expected_inconclusive_results() {
  Fixture fixture;
  const primo::smt::Tactic unsupported = primo::smt::make_solver_tactic(
      "unsupported", fixture.terms, [](const primo::smt::Goal&) -> primo::smt::SolverAnswer {
        throw primo::smt::UnsupportedInput("outside fragment");
      });
  const primo::smt::AnyOf result =
      unsupported(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));

  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::Unsupported);
  const primo::smt::CheckSatOutcome outcome =
      primo::smt::outcome_from_tactic_result(result, fixture.context);
  check(outcome.result == primo::smt::CheckSatResult::Unsupported);
  check(outcome.message == "outside fragment");
}

void typed_inconclusive_reason_overrides_terminal_formula_truth() {
  Fixture fixture;
  for (const primo::smt::Goal& terminal :
       {primo::smt::Goal{}, primo::smt::Goal{{fixture.terms.mk_bool(false)}}}) {
    const primo::smt::AnyOf cancelled = primo::smt::AnyOf::single(
        terminal.with_diagnostic(primo::smt::InconclusiveReason::Cancelled, "cancelled"));
    const primo::smt::CheckSatOutcome outcome =
        primo::smt::outcome_from_tactic_result(cancelled, fixture.context);
    check(outcome.result == primo::smt::CheckSatResult::Unknown);
    check(outcome.message == "cancelled");
  }
}

void preprocessing_fixed_point_and_terminal_change_statistics_are_exact() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_passes = 4;

  const primo::smt::CheckSatOutcome stable = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (not (= a b)))
    (check-sat)
  )",
                                                                          options);
  check(stable.result == primo::smt::CheckSatResult::Sat);
  check(stable.statistics.get(primo::util::StatKey::PreprocessingPasses) == 1);
  check(stable.statistics.get(primo::util::StatKey::PreprocessingChangedPasses) == 0);
  check(stable.statistics.get(":tactic.preprocessing-round.invocations") == 1);
  check(stable.statistics.get(":tactic.forced-atom-propagation.invocations") == 1);

  const primo::smt::CheckSatOutcome contradiction = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun f (A A) A)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun d () A)
    (assert (= a b))
    (assert (= c d))
    (assert (not (= (f a c) (f b d))))
    (check-sat)
  )",
                                                                                 options);
  check(contradiction.result == primo::smt::CheckSatResult::Unsat);
  check(contradiction.statistics.get(primo::util::StatKey::PreprocessingPasses) == 1);
  check(contradiction.statistics.get(primo::util::StatKey::PreprocessingChangedPasses) == 1);
  check(contradiction.statistics.get(primo::util::StatKey::PreprocessingAdaptivePasses) == 0);
}

void solver_adapter_does_not_hide_serious_failures() {
  Fixture fixture;
  const primo::smt::Tactic broken = primo::smt::make_solver_tactic(
      "broken", fixture.terms, [](const primo::smt::Goal&) -> primo::smt::SolverAnswer {
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
  true_reconstruction.conversions =
      true_reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = true, .filtered = false, .directed_substitution = false}}});
  primo::smt::ModelReconstruction false_reconstruction;
  false_reconstruction.conversions =
      false_reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = false, .filtered = false, .directed_substitution = false}}});
  const primo::smt::Tactic round =
      primo::smt::make_preprocessing_round(primo::smt::PreprocessingRoundOptions{});

  const primo::smt::AnyOf result = round(
      fixture.context,
      primo::smt::AnyOf({primo::smt::Goal{{p}}.with_reconstruction(true_reconstruction),
                         primo::smt::Goal{{not_p}}.with_reconstruction(false_reconstruction)}));
  check(result.size() == 2);
  const auto true_assignments =
      result.alternatives()[0].reconstruction().conversions.theory_assignments(fixture.terms);
  const auto false_assignments =
      result.alternatives()[1].reconstruction().conversions.theory_assignments(fixture.terms);
  check(true_assignments.size() == 1);
  check(false_assignments.size() == 1);
  check(true_assignments.front().value);
  check(!false_assignments.front().value);
  check(fixture.statistics.get(":tactic.preprocessing-round.invocations") == 1);
  check(fixture.statistics.get(":tactic.boolean-unit-propagation.invocations") == 1);
  check(fixture.statistics.get(":tactic.forced-atom-propagation.invocations") == 1);
  check(fixture.statistics.get(":tactic.unit-equality-propagation.invocations") == 1);
}

void public_preprocessing_leaves_are_independently_constructible_and_typed() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::core::TermId nested = fixture.terms.mk_and({p, fixture.terms.mk_and({p, p})});

  const primo::smt::NormalizeBooleanOptions boolean_options{
      .stage = primo::smt::BooleanNormalizationStage::Initial,
      .flatten_threshold = 8,
      .implication_mode = primo::smt::BooleanImpliesRewriteMode::Off};
  const primo::smt::Strategy boolean_strategy = primo::smt::Strategy::leaf(
      "normalize-boolean", boolean_options, primo::smt::make_normalize_boolean(boolean_options));
  const primo::smt::AnyOf normalized = primo::smt::compile(boolean_strategy)(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{nested}}));
  check(normalized.alternatives().front().assertions().size() == 1);
  check(boolean_strategy.canonical().find("flatten-threshold") != std::string::npos);

  const primo::smt::AnyOf propagated = primo::smt::make_propagate_values()(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}}));
  check(propagated.size() == 1);
  check(fixture.statistics.get(":tactic.propagate-values.invocations") == 1);

  const primo::smt::SolveEqsOptions solve_options{.solved_constant_substitution = true,
                                                  .gaussian_elimination = false,
                                                  .use_ground_unit_substitution = false,
                                                  .use_guarded_ground_unit_substitution = false};
  static_cast<void>(primo::smt::make_solve_eqs(solve_options)(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}})));
  check(fixture.statistics.get(":tactic.solve-eqs.invocations") == 1);

  static_cast<void>(
      primo::smt::make_derive_equalities({.equality_bridge = true, .unit_consequences = true})(
          fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}})));
  check(fixture.statistics.get(":tactic.derive-equalities.invocations") == 1);

  static_cast<void>(primo::smt::make_finite_domain_preprocess(
      {.evaluate_equalities = true, .generate_ground_congruence = true})(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}})));
  check(fixture.statistics.get(":tactic.finite-domain-preprocess.invocations") == 1);

  const primo::core::SymbolId x_symbol =
      fixture.symbols.declare_constant("x", fixture.symbols.real_sort());
  const primo::core::TermId x = fixture.terms.mk_constant(x_symbol);
  const primo::core::TermId bound = fixture.terms.mk_le(x, fixture.terms.mk_rational(0));
  primo::smt::TacticContext model_context(fixture.terms, fixture.statistics, {}, true);
  const primo::smt::AnyOf eliminated = primo::smt::make_eliminate_monotone()(
      model_context, primo::smt::AnyOf::single(primo::smt::Goal{{bound}}));
  check(eliminated.alternatives().front().reconstruction().conversions.size() <= 1);
  check(fixture.statistics.get(":tactic.eliminate-monotone.invocations") == 1);

  const primo::smt::NormalizeArithmeticOptions arithmetic_options{
      .stage = primo::smt::ArithmeticNormalizationStage::Catchup,
      .canonicalize = true,
      .lower_real_equalities = true,
      .guarded_real_equality_lowering = false};
  static_cast<void>(primo::smt::make_normalize_arithmetic(arithmetic_options)(
      model_context, primo::smt::AnyOf::single(primo::smt::Goal{{bound}})));
  check(fixture.statistics.get(":tactic.normalize-arithmetic.invocations") == 1);

  primo::smt::Frontend frontend;
  primo::util::Statistics purification_statistics;
  primo::smt::TacticContext purification_context(frontend.terms(), purification_statistics);
  const primo::core::TermId trivial_bound =
      frontend.terms().mk_le(frontend.terms().mk_rational(0), frontend.terms().mk_rational(1));
  static_cast<void>(primo::smt::make_purify_mixed(
      frontend, primo::smt::PurifyMixedOptions{.canonicalize_arithmetic_after = true})(
      purification_context, primo::smt::AnyOf::single(primo::smt::Goal{{trivial_bound}})));
  check(purification_statistics.get(":tactic.purify-mixed.invocations") == 1);
  check(purification_statistics.get(":no.purification.skipped-no-sites") == 1);
}

void production_preprocessing_profile_and_strategy_are_explicit_and_stable() {
  primo::smt::Frontend frontend;
  primo::core::TermManager& terms = frontend.terms();
  const primo::core::TermId bound = terms.mk_le(terms.mk_rational(0), terms.mk_rational(1));
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.preprocessing_passes = 1;
  options.preprocessing_adaptive_extra_passes = 2;
  options.monotone_elimination = true;
  primo::util::Statistics statistics;
  const primo::smt::PreprocessingProfile profile =
      primo::smt::analyze_preprocessing_profile(terms, {bound}, options, false);
  check(profile.kind == primo::smt::PreprocessingProfileKind::Strong);
  check(profile.strong_preprocessing);
  check(profile.preprocessing_passes == 3);
  const primo::smt::Strategy strategy =
      primo::smt::make_preprocessing_strategy(frontend, options, profile, {bound}, statistics);
  const std::string canonical = strategy.canonical();
  const std::size_t profile_position = canonical.find("preprocessing-profile");
  const std::size_t repeat_position = canonical.find("adaptive-reachable-term-shrink");
  const std::size_t finite_position = canonical.find("finite-domain-preprocess");
  const std::size_t arithmetic_position = canonical.find("normalize-arithmetic");
  const std::size_t monotone_position = canonical.find("eliminate-monotone");
  const std::size_t purify_position = canonical.find("purify-mixed");
  const std::size_t boolean_position = canonical.find("normalize-boolean");
  check(profile_position < repeat_position);
  check(repeat_position < finite_position);
  check(finite_position < arithmetic_position);
  check(arithmetic_position < monotone_position);
  check(monotone_position < purify_position);
  check(purify_position < boolean_position);
  check(canonical.find("(repeat 5 \"adaptive-reachable-term-shrink\"") != std::string::npos);
  check(canonical.find("\"equality-definitions\" \"true\"") != std::string::npos);
  check(strategy.fingerprint() == strategy.fingerprint());

  primo::smt::CheckSatOptions bypass_options;
  bypass_options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  bypass_options.auto_preprocess_tuning.large_conjunct_limit = 1;
  const primo::smt::PreprocessingProfile bypass =
      primo::smt::analyze_preprocessing_profile(terms, {bound, bound}, bypass_options, false);
  check(bypass.kind == primo::smt::PreprocessingProfileKind::LargeInputBypass);
  check(!bypass.preprocessing_loop_enabled);
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
  goal_updates_share_untouched_large_payloads();
  conversion_chain_replays_typed_steps_in_reverse_execution_order();
  anyof_has_disjunctive_semantics();
  empty_anyof_is_rejected();
  non_boolean_goal_assertion_is_a_contract_error();
  sequence_and_repeat_stop_at_a_fixed_point();
  repeat_preserves_equivalent_output_and_observes_terminal_changes();
  conditional_uses_a_probe();
  strategy_is_immutable_canonical_and_fingerprinted();
  strategy_probe_observes_the_transformed_goal_and_repeat_is_bounded();
  try_for_uses_context_deadlines_without_mutating_the_strategy();
  solver_leaf_handles_disjunctive_goals_in_order();
  sequential_scheduler_tries_each_arm_from_the_original_goal();
  cancellation_stops_before_running_a_tactic();
  check_sat_honors_an_existing_cancellation_request();
  check_sat_preserves_cancellation_observed_by_preprocessing();
  solver_adapter_types_expected_inconclusive_results();
  typed_inconclusive_reason_overrides_terminal_formula_truth();
  preprocessing_fixed_point_and_terminal_change_statistics_are_exact();
  solver_adapter_does_not_hide_serious_failures();
  preprocessing_round_transforms_each_alternative_independently();
  public_preprocessing_leaves_are_independently_constructible_and_typed();
  production_preprocessing_profile_and_strategy_are_explicit_and_stable();
  solver_dispatch_uses_formula_tactics();
}
