#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "core/ids.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smt {

struct TheoryModelAssignment {
  core::TermId term = core::kInvalidTermId;
  bool value = false;
  bool filtered = false;
  bool directed_substitution = false;

  bool operator==(const TheoryModelAssignment&) const = default;
};

struct MonotoneEliminatedVariable {
  core::TermId variable = core::kInvalidTermId;
  bool bounded_above = false;
  std::vector<std::pair<core::TermId, bool>> constraints;

  bool operator==(const MonotoneEliminatedVariable&) const = default;
};

struct BooleanTheoryAssignmentConversion {
  std::vector<TheoryModelAssignment> assignments;

  bool operator==(const BooleanTheoryAssignmentConversion&) const = default;
};

struct DirectedEqualitySubstitutionConversion {
  std::vector<std::pair<core::TermId, core::TermId>> substitutions;

  bool operator==(const DirectedEqualitySubstitutionConversion&) const = default;
};

struct LiaSubstitutionConversion {
  std::vector<std::pair<core::TermId, core::TermId>> substitutions;

  bool operator==(const LiaSubstitutionConversion&) const = default;
};

struct MonotoneEliminationConversion {
  std::vector<MonotoneEliminatedVariable> eliminated;

  bool operator==(const MonotoneEliminationConversion&) const = default;
};

using ModelConversionStep =
    std::variant<BooleanTheoryAssignmentConversion, DirectedEqualitySubstitutionConversion,
                 LiaSubstitutionConversion, MonotoneEliminationConversion>;

// Persistent execution-ordered postsolve log. Transformations append one typed
// step; model builders replay steps in reverse without regrouping them by kind.
class ModelConversionChain {
 public:
  ModelConversionChain appended(ModelConversionStep step) const;

  bool empty() const { return tail_ == nullptr; }
  std::size_t size() const { return tail_ == nullptr ? 0 : tail_->size; }

  template <typename Visitor>
  void replay_reverse(Visitor&& visitor) const {
    for (std::shared_ptr<const Node> node = tail_; node != nullptr; node = node->previous) {
      std::forward<Visitor>(visitor)(node->step);
    }
  }

  // Compatibility projection for the current string model builders. It keeps
  // execution order and materializes directed equalities as assignment facts.
  std::vector<TheoryModelAssignment> theory_assignments(core::TermManager& terms) const;

  bool operator==(const ModelConversionChain& other) const;

 private:
  struct Node {
    ModelConversionStep step;
    std::shared_ptr<const Node> previous;
    std::size_t size = 0;
  };

  std::shared_ptr<const Node> tail_;
};

// Forced facts are analysis input for the later finite-domain tactic, not model
// conversions, so they remain outside the ordered postsolve chain.
struct ModelReconstruction {
  ModelConversionChain conversions;
  std::vector<std::pair<core::TermId, core::TermId>> forced_equalities;
  std::vector<std::pair<core::TermId, core::TermId>> forced_disequalities;

  bool operator==(const ModelReconstruction&) const = default;
};

}  // namespace primo::smt
