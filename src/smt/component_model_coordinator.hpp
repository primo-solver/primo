#pragma once

#include <cstdint>
#include <span>

#include "smt/model_based_combination.hpp"
#include "smt/theory_combination_plan.hpp"
#include "theory/theory_component.hpp"

namespace primo::smt {

// Collection order is a recipe priority, not a theory-name dispatch. The plan
// and component order must match. See docs/n-way-combination-migration.md.
class ComponentModelCoordinator {
 public:
  [[nodiscard]] ModelBasedCombinationResult reconcile(
      const TheoryCombinationPlan& plan, std::uint64_t assignment_epoch,
      std::span<theory::TheoryComponent* const> components,
      std::span<const theory::InterfaceRelation> arrangement,
      const EqualityLiteralResolver& equality_literal);
  [[nodiscard]] const ModelBasedCombinationStatistics& statistics() const { return statistics_; }

 private:
  ModelBasedCombinationStatistics statistics_;
};

}  // namespace primo::smt
