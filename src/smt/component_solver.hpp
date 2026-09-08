#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"
#include "smt/model_reconstruction.hpp"
#include "theory/theory_component.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

struct CombinationRecipe {
  // Factory order is the model reconciliation priority; online_order is an
  // independent permutation. The scheduler has no theory-name branches.
  std::vector<std::function<std::unique_ptr<theory::TheoryComponent>(bool)>> factories;
  std::vector<std::size_t> online_order;
  bool online = false;
  bool model_guided = true;
  std::size_t decision_budget = 0;
  std::string timing_prefix;
  std::function<void(BooleanClausifier&)> prepare_boolean;
};

CheckSatOutcome solve_component_recipe(core::TermManager& terms,
                                       std::span<const core::TermId> assertions,
                                       const ModelConversionChain& conversions,
                                       const CheckSatOptions& options, util::Statistics& statistics,
                                       const CombinationRecipe& recipe);

}  // namespace primo::smt
