#pragma once

#include <optional>
#include <string>

#include "smt/check_sat.hpp"
#include "smt/tactic.hpp"

namespace primo::smt {

struct SolverTacticSet {
  Tactic boolean_solver;
  Tactic euf_solver;
  Tactic lra_solver;
  Tactic lra_model_only_solver;
  Tactic mixed_solver;
  Tactic lia_solver;
};

struct SolverStrategyOptions {
  std::optional<std::string> logic;
  MixedDispatchMode mixed_dispatch = MixedDispatchMode::Narrow;
  bool produce_model = false;
  bool has_declared_real_constants = false;
};

// Builds the formula-feature probe and solver-family selection strategy. Each
// alternative is classified independently before it is passed to a leaf.
Tactic make_solver_dispatch(SolverStrategyOptions options, SolverTacticSet solvers);

}  // namespace primo::smt
