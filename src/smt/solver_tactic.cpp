#include "smt/solver_tactic.hpp"

#include <optional>
#include <string>
#include <utility>

#include "core/term_manager.hpp"
#include "smt/solver_error.hpp"

namespace primo::smt {

Tactic make_solver_tactic(std::string name, core::TermManager& terms, GoalSolver solve) {
  return make_goal_tactic(std::move(name), [&terms, solve = std::move(solve)](
                                               TacticContext&, const Goal& goal) mutable {
    CheckSatOutcome outcome;
    try {
      outcome = solve(goal);
    } catch (const UnsupportedInput& error) {
      outcome.result = CheckSatResult::Unsupported;
      outcome.message = error.what();
    }

    const InconclusiveReason reason =
        outcome.result == CheckSatResult::Unsupported ? InconclusiveReason::Unsupported
        : outcome.result == CheckSatResult::Unknown
            ? (outcome.message == "theory conflict limit reached"
                   ? InconclusiveReason::ResourceLimit
               : outcome.message == "cancelled" ? InconclusiveReason::Cancelled
                                                : InconclusiveReason::Unknown)
            : InconclusiveReason::None;
    Goal result = goal.with_model(outcome.model.empty()
                                      ? std::optional<std::string>{}
                                      : std::optional<std::string>{std::move(outcome.model)})
                      .with_diagnostic(reason, std::move(outcome.message));
    if (outcome.result == CheckSatResult::Sat) {
      result = result.with_assertions({});
    } else if (outcome.result == CheckSatResult::Unsat) {
      result = result.with_assertions({terms.mk_bool(false)});
    }
    return AnyOf::single(std::move(result));
  });
}

CheckSatOutcome outcome_from_solver_result(const AnyOf& solved, TacticContext& context) {
  if (solved.size() != 1) {
    throw TacticError("solver dispatch returned multiple alternatives");
  }

  const GoalTruth solved_truth = truth(solved, context);
  const Goal& solved_goal = solved.alternatives().front();
  CheckSatOutcome outcome;
  outcome.model = solved_goal.model().value_or("");
  outcome.message = solved_goal.diagnostic();
  if (solved_truth == GoalTruth::True) {
    outcome.result = CheckSatResult::Sat;
  } else if (solved_truth == GoalTruth::False) {
    outcome.result = CheckSatResult::Unsat;
  } else if (solved_goal.reason() == InconclusiveReason::Unsupported) {
    outcome.result = CheckSatResult::Unsupported;
  } else {
    outcome.result = CheckSatResult::Unknown;
  }
  return outcome;
}

}  // namespace primo::smt
