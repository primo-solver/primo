#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/model.hpp"
#include "theory/theory_solver.hpp"

namespace primo::theory {

enum class EqualityPropagationStrength { None, Opportunistic, Complete };

struct ComponentSharedSort {
  core::SortId sort = core::kInvalidSortId;
  // A Yes declaration is joint across all shared sorts of this component.
  StableInfiniteness stable_infiniteness = StableInfiniteness::No;
  bool authoritative_values = false;
  bool operator==(const ComponentSharedSort&) const = default;
};

struct ComponentCapabilities {
  bool model_projection = false;
  bool model_export = false;
  bool model_mutation = false;
  bool care_pairs = false;
  bool incremental = false;
  bool interface_extension = false;
  EqualityPropagationStrength equality_propagation = EqualityPropagationStrength::None;
  bool operator==(const ComponentCapabilities&) const = default;
};

struct ComponentDescriptor {
  std::string id;
  std::string fragment;
  // Owned interpreted signature families and uninterpreted function symbols.
  // Equality, Boolean structure and purified free constants are SMT-owned.
  std::vector<std::string> signature_families;
  std::vector<core::SymbolId> function_symbols;
  std::vector<ComponentSharedSort> shared_sorts;
  Convexity convexity = Convexity::NonConvex;
  ComponentCapabilities capabilities;
  bool operator==(const ComponentDescriptor&) const = default;
};

struct ComponentInterfaceScope {
  core::SortId sort = core::kInvalidSortId;
  std::vector<core::TermId> terms;
};

struct ComponentAtom {
  core::TermId term = core::kInvalidTermId;
  sat::Lit lit;
};

struct ComponentModelBinding {
  core::TermId term = core::kInvalidTermId;
  core::ModelValue value;
};

struct ComponentModelFact {
  core::TermId atom = core::kInvalidTermId;
  bool value = false;
};

// Owning contribution. DomainValue indices are local to token.component until
// the SMT assembler remaps them. Numeric/Boolean bindings are authoritative;
// an EUF component exports structural facts, not invented numeric anchors.
struct ComponentModelFragment {
  TheoryModelToken token;
  std::vector<ComponentModelBinding> bindings;
  std::vector<ComponentModelFact> facts;
  std::vector<core::ModelInterpretation> interpretations;
};

// Original contract: docs/n-way-combination-migration.md. Legacy TheorySolver
// remains separate because its production QF_UFLRA users are frozen.
class TheoryComponent {
 public:
  virtual ~TheoryComponent() = default;
  [[nodiscard]] virtual const ComponentDescriptor& descriptor() const = 0;
  [[nodiscard]] virtual bool owns_atom(core::TermId atom) const = 0;
  [[nodiscard]] virtual std::vector<core::TermId> interface_variables(core::TermId atom) const = 0;

  // Registration is at a detached/root preparation boundary, never a live
  // tableau mutation. Implementations must add unconstrained missing variables.
  virtual void register_interface(std::span<const ComponentInterfaceScope> scopes) = 0;
  virtual void register_atoms(std::span<const ComponentAtom> atoms) = 0;
  // Return a pure Boolean formula equivalent to the shared equality in this
  // component. SMT owns clausification and all SAT backend operations.
  [[nodiscard]] virtual core::TermId encode_equality(const InterfacePair& pair) = 0;
  virtual void reset() = 0;
  virtual void push_level() = 0;
  virtual void pop_to_level(std::size_t level) = 0;
  virtual void assign(std::span<const AssignedTheoryLiteral> delta) = 0;
  [[nodiscard]] virtual TheoryCheckResult check(TheoryCheckEffort effort,
                                                std::uint64_t assignment_epoch,
                                                std::span<const InterfaceRelation> arrangement) = 0;
  // Cheap bound/closure consequences precede an expensive Partial check.
  // Reasons contain true antecedents, not their clause negations.
  [[nodiscard]] virtual std::optional<TheoryCheckResult> propagate() { return std::nullopt; }
  [[nodiscard]] virtual sat::Lit suggest_literal() { return {}; }
  [[nodiscard]] virtual TheoryMutationResult mutate(const TheoryMutationRequest&) { return {}; }
  [[nodiscard]] virtual std::vector<InterfacePair> care_pairs(const TheoryModelToken&) const {
    return {};
  }
  [[nodiscard]] virtual ComponentModelFragment export_model(
      const TheoryModelToken& token) const = 0;
};

}  // namespace primo::theory
