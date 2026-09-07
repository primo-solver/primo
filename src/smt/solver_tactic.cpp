#include "smt/solver_tactic.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/solver_answer.hpp"
#include "smt/solver_error.hpp"
#include "smt/tactic.hpp"

namespace primo::smt {

Tactic make_solver_tactic(std::string name, core::TermManager& terms, GoalSolver solve) {
  return Tactic(std::move(name), [&terms, solve = std::move(solve)](TacticContext&,
                                                                    const AnyOf& input) mutable {
    std::vector<Goal> inconclusive;
    for (const Goal& goal : input.alternatives()) {
      if (goal.assertions().empty()) {
        return AnyOf::single(goal);
      }
      SolverAnswer answer;
      try {
        answer = solve(goal);
      } catch (const UnsupportedInput& error) {
        answer = SolverAnswer{.status = SolverStatus::Inconclusive,
                              .reason = InconclusiveReason::Unsupported,
                              .diagnostic = error.what(),
                              .model = {}};
      }

      Goal result = goal.with_model(answer.model.empty()
                                        ? std::optional<std::string>{}
                                        : std::optional<std::string>{std::move(answer.model)})
                        .with_typed_model(std::move(answer.typed_model))
                        .with_diagnostic(answer.status == SolverStatus::Inconclusive
                                             ? answer.reason
                                             : InconclusiveReason::None,
                                         std::move(answer.diagnostic));
      if (answer.status == SolverStatus::Sat) {
        return AnyOf::single(result.with_assertions({}));
      }
      if (answer.status == SolverStatus::Inconclusive) {
        inconclusive.push_back(std::move(result));
      }
    }
    if (inconclusive.empty()) {
      return AnyOf::single(Goal({terms.mk_bool(false)}));
    }
    return AnyOf(std::move(inconclusive));
  });
}

SolverAnswer solver_answer_from_outcome(CheckSatOutcome outcome) {
  if (outcome.result == CheckSatResult::Sat) {
    return SolverAnswer{.status = SolverStatus::Sat,
                        .reason = InconclusiveReason::None,
                        .diagnostic = std::move(outcome.message),
                        .model = std::move(outcome.model),
                        .typed_model = std::move(outcome.typed_model)};
  }
  if (outcome.result == CheckSatResult::Unsat) {
    return SolverAnswer{.status = SolverStatus::Unsat,
                        .reason = InconclusiveReason::None,
                        .diagnostic = std::move(outcome.message),
                        .model = {}};
  }
  InconclusiveReason reason = outcome.reason;
  if (reason == InconclusiveReason::None) {
    if (outcome.result == CheckSatResult::Unsupported) {
      reason = InconclusiveReason::Unsupported;
    } else {
      reason = InconclusiveReason::Unknown;
    }
  }
  return SolverAnswer{.status = SolverStatus::Inconclusive,
                      .reason = reason,
                      .diagnostic = std::move(outcome.message),
                      .model = {}};
}

CheckSatOutcome outcome_from_tactic_result(const AnyOf& solved, TacticContext& context) {
  const GoalTruth solved_truth = truth(solved, context);
  CheckSatOutcome outcome;
  for (const Goal& goal : solved.alternatives()) {
    if (goal.reason() != InconclusiveReason::None) {
      outcome.result = goal.reason() == InconclusiveReason::Unsupported
                           ? CheckSatResult::Unsupported
                           : CheckSatResult::Unknown;
      outcome.message = goal.diagnostic();
      outcome.reason = goal.reason();
      return outcome;
    }
  }
  if (solved_truth == GoalTruth::True) {
    outcome.result = CheckSatResult::Sat;
    for (const Goal& goal : solved.alternatives()) {
      if (truth(goal, context) == GoalTruth::True) {
        outcome.model = goal.model().value_or("");
        outcome.typed_model = goal.typed_model();
        outcome.message = goal.diagnostic();
        break;
      }
    }
  } else if (solved_truth == GoalTruth::False) {
    outcome.result = CheckSatResult::Unsat;
  } else {
    outcome.result = CheckSatResult::Unknown;
    outcome.reason = InconclusiveReason::Unknown;
    const Goal* selected = &solved.alternatives().front();
    outcome.message = selected->diagnostic();
  }
  return outcome;
}

}  // namespace primo::smt
