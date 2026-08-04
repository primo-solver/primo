#pragma once

#include "core/ids.hpp"

namespace primo::theory {

enum class ModelEqualityStatus {
  kNotEqual,
  kEqualMutationUnknown,
  kEqualMutationAvoidable,
  kEqualMutationForced,
};

struct ModelEqualityEvaluation {
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
  ModelEqualityStatus status = ModelEqualityStatus::kNotEqual;

  [[nodiscard]] bool equal_in_model() const { return status != ModelEqualityStatus::kNotEqual; }

  [[nodiscard]] bool avoidable_by_mutation() const {
    return status == ModelEqualityStatus::kEqualMutationAvoidable;
  }

  [[nodiscard]] bool forced_after_mutation_filter() const {
    return status == ModelEqualityStatus::kEqualMutationForced;
  }
};

class TheorySolver {};

}  // namespace primo::theory
