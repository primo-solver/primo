#include "smt/solver_strategy.hpp"

#include <memory>
#include <unordered_map>
#include <utility>

#include "core/ids.hpp"
#include "smt/formula_analysis.hpp"

namespace primo::smt {

Tactic make_solver_dispatch(SolverStrategyOptions options, SolverTacticSet solvers) {
  const auto scheduler = std::make_shared<SequentialTacticScheduler>();
  const Tactic boolean_then_euf =
      first_terminal("boolean-then-euf", scheduler, {solvers.boolean_solver, solvers.euf_solver});

  return make_goal_tactic("solver-dispatch", [options = std::move(options),
                                              solvers = std::move(solvers), boolean_then_euf](
                                                 TacticContext& context, const Goal& goal) {
    FormulaFeatureSummary features;
    std::unordered_map<core::TermId, FormulaFeatureSummary> memo;
    for (const core::TermId assertion : goal.assertions()) {
      features |=
          summarize_formula_features(context.terms(), assertion, memo, options.mixed_dispatch);
    }

    const Tactic* selected = nullptr;
    if (options.logic == "QF_LIA") {
      selected = &solvers.lia_solver;
    } else if ((features.has_equality || features.has_euf_predicate) && features.has_arithmetic) {
      selected = &solvers.mixed_solver;
    } else if (features.has_arithmetic) {
      selected = &solvers.lra_solver;
    } else if (options.produce_model && options.has_declared_real_constants &&
               !features.has_equality && !features.has_euf_predicate) {
      selected = &solvers.lra_model_only_solver;
    } else if (features.has_equality || features.has_application_term) {
      selected = &solvers.euf_solver;
    } else {
      selected = &boolean_then_euf;
    }
    return (*selected)(context, AnyOf::single(goal));
  });
}

}  // namespace primo::smt
