#pragma once

#include <functional>
#include <string>

#include "smt/check_sat.hpp"
#include "smt/tactic.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smt {

using GoalSolver = std::function<CheckSatOutcome(const Goal&)>;

// Adapts an ordinary solver implementation to the formula-valued tactic
// contract. UnsupportedInput is the only expected exception translated into
// an inconclusive result; contract and backend failures continue to escape.
Tactic make_solver_tactic(std::string name, core::TermManager& terms, GoalSolver solve);

// Converts a single solver goal back to the public result record. Timing and
// statistics remain the solve-session owner's responsibility.
CheckSatOutcome outcome_from_solver_result(const AnyOf& solved, TacticContext& context);

}  // namespace primo::smt
