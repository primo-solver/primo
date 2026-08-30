#include "smt/solver_tactic.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/term_manager.hpp"
#include "smt/solver_error.hpp"

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

SolverAnswer solver_answer_from_outcome(CheckSatOutcome outcome, const TacticContext& context) {
  if (outcome.result == CheckSatResult::Sat) {
    return SolverAnswer{.status = SolverStatus::Sat,
                        .reason = InconclusiveReason::None,
                        .diagnostic = std::move(outcome.message),
                        .model = std::move(outcome.model)};
  }
  if (outcome.result == CheckSatResult::Unsat) {
    return SolverAnswer{.status = SolverStatus::Unsat,
                        .reason = InconclusiveReason::None,
                        .diagnostic = std::move(outcome.message),
                        .model = {}};
  }
  const InconclusiveReason reason = outcome.result == CheckSatResult::Unsupported
                                        ? InconclusiveReason::Unsupported
                                    : context.cancelled()        ? InconclusiveReason::Cancelled
                                    : context.deadline_reached() ? InconclusiveReason::ResourceLimit
                                                                 : InconclusiveReason::Unknown;
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
      return outcome;
    }
  }
  if (solved_truth == GoalTruth::True) {
    outcome.result = CheckSatResult::Sat;
    for (const Goal& goal : solved.alternatives()) {
      if (truth(goal, context) == GoalTruth::True) {
        outcome.model = goal.model().value_or("");
        outcome.message = goal.diagnostic();
        break;
      }
    }
  } else if (solved_truth == GoalTruth::False) {
    outcome.result = CheckSatResult::Unsat;
  } else {
    outcome.result = CheckSatResult::Unknown;
    const Goal* selected = &solved.alternatives().front();
    outcome.message = selected->diagnostic();
  }
  return outcome;
}

}  // namespace primo::smt
