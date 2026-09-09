#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "smt/model_based_combination.hpp"
#include "smt/theory_combination_plan.hpp"
#include "theory/theory_component.hpp"

namespace primo::smt {

struct ComponentModelStatistics : ModelBasedCombinationStatistics {
  std::size_t care_pair_agreements = 0;
  std::size_t spanning_pair_checks = 0;
};

// Collection order is a recipe priority, not a theory-name dispatch. The plan
// and component order must match. See docs/n-way-combination-migration.md.
class ComponentModelCoordinator {
 public:
  [[nodiscard]] ModelBasedCombinationResult reconcile(
      const TheoryCombinationPlan& plan, std::uint64_t assignment_epoch,
      std::span<theory::TheoryComponent* const> components,
      std::span<const theory::InterfaceRelation> arrangement,
      const EqualityLiteralResolver& equality_literal);
  [[nodiscard]] const ComponentModelStatistics& statistics() const { return statistics_; }
  [[nodiscard]] const std::vector<theory::TheoryModelToken>& model_tokens() const {
    return tokens_;
  }
  [[nodiscard]] const std::vector<theory::TheoryModelProjection>& model_projections() const {
    return projections_;
  }

 private:
  ComponentModelStatistics statistics_;
  std::vector<theory::TheoryModelToken> tokens_;
  std::vector<theory::TheoryModelProjection> projections_;
};

}  // namespace primo::smt
