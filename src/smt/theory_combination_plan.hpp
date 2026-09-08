#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/term_manager.hpp"
#include "theory/theory_component.hpp"

namespace primo::smt {

class UnsupportedCombination : public std::runtime_error {
 public:
  explicit UnsupportedCombination(const std::string& message) : std::runtime_error(message) {}
};

enum class CombinationPolicy { ModelGuided, CompleteArrangement };

struct CombinationScope {
  core::SortId sort = core::kInvalidSortId;
  std::vector<core::TermId> terms;
  std::vector<std::size_t> participants;
};

// Immutable solve recipe after validation; all descriptors/scopes/routes are
// owned. Component pointers are borrowed only during construction/registration.
// Normalization basis and complexity: docs/n-way-combination-migration.md.
class TheoryCombinationPlan {
 public:
  TheoryCombinationPlan(const core::TermManager& terms,
                        std::span<theory::TheoryComponent* const> components,
                        std::span<const theory::ComponentAtom> atoms, bool model_guided);

  [[nodiscard]] CombinationPolicy policy() const { return policy_; }
  [[nodiscard]] bool convex_complete_propagation() const;
  [[nodiscard]] const std::vector<theory::ComponentDescriptor>& descriptors() const {
    return descriptors_;
  }
  [[nodiscard]] const std::vector<CombinationScope>& scopes() const { return scopes_; }
  [[nodiscard]] const std::vector<theory::ComponentInterfaceScope>& component_scopes(
      std::size_t component) const {
    return component_scopes_.at(component);
  }
  [[nodiscard]] const std::vector<theory::ComponentAtom>& component_atoms(
      std::size_t component) const {
    return component_atoms_.at(component);
  }
  [[nodiscard]] std::size_t owner(core::TermId atom) const;
  [[nodiscard]] std::optional<std::size_t> scope_for(const theory::InterfacePair& pair) const;
  void register_interfaces(std::span<theory::TheoryComponent* const> components) const;

 private:
  CombinationPolicy policy_ = CombinationPolicy::CompleteArrangement;
  std::vector<theory::ComponentDescriptor> descriptors_;
  std::vector<CombinationScope> scopes_;
  std::vector<std::vector<theory::ComponentInterfaceScope>> component_scopes_;
  std::vector<std::vector<theory::ComponentAtom>> component_atoms_;
  std::unordered_map<core::TermId, std::size_t> owners_;
  std::unordered_map<core::TermId, std::size_t> scope_by_term_;
};

}  // namespace primo::smt
