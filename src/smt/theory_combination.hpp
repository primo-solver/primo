#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/ids.hpp"
#include "sat/lit.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smt {

struct InterfaceTerm {
  core::TermId term = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;
};

struct InterfaceEquality {
  core::TermId atom = core::kInvalidTermId;
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;
};

enum class InterfaceEqualityStatus {
  Missing,
  Visible,
};

enum class InterfaceEqualityValue {
  Unknown,
  Equal,
  Disequal,
};

struct InterfaceEqualityRegistryEntry {
  core::TermId atom = core::kInvalidTermId;
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;
  sat::Lit lit;
  InterfaceEqualityStatus status = InterfaceEqualityStatus::Missing;
  InterfaceEqualityValue value = InterfaceEqualityValue::Unknown;
};

enum class PurificationSiteKind {
  RealTermInEufAtom,
  UninterpretedApplicationInArithmeticAtom,
};

struct PurificationSite {
  core::TermId term = core::kInvalidTermId;
  core::TermId atom = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;
  PurificationSiteKind kind = PurificationSiteKind::RealTermInEufAtom;
};

struct NelsonOppenInterfaceSummary {
  std::vector<InterfaceTerm> terms;
  std::vector<InterfaceEquality> visible_equalities;
  std::vector<InterfaceEqualityRegistryEntry> equality_registry;
  std::vector<PurificationSite> purification_sites;
  std::size_t sort_count = 0;
  std::size_t candidate_pairs = 0;
  std::size_t missing_equalities = 0;
  std::size_t euf_real_terms = 0;
  std::size_t arithmetic_uninterpreted_applications = 0;
};

class TheoryCombinationManager {
 public:
  explicit TheoryCombinationManager(const core::TermManager& terms);

  void rebuild(const std::vector<core::TermId>& assertions);
  void rebuild_from_interface_terms(const std::vector<core::TermId>& interface_terms);

  const NelsonOppenInterfaceSummary& summary() const { return summary_; }

  std::optional<InterfaceEqualityRegistryEntry> interface_equality(core::TermId lhs,
                                                                   core::TermId rhs) const;
  std::optional<InterfaceEqualityRegistryEntry> interface_equality(core::TermId equality) const;
  std::optional<sat::Lit> interface_equality_lit(core::TermId lhs, core::TermId rhs) const;
  std::optional<sat::Lit> interface_equality_lit(core::TermId equality) const;
  bool register_interface_equality_atom(core::TermId equality, sat::Lit lit);
  InterfaceEqualityValue interface_equality_value(core::TermId lhs, core::TermId rhs) const;
  bool set_interface_equality_value(core::TermId lhs, core::TermId rhs,
                                    InterfaceEqualityValue value);
  void clear_interface_equality_values();
  void push_level();
  void pop_to_level(std::size_t new_level);

 private:
  struct EqualityValueTrailEntry {
    std::size_t index = 0;
    InterfaceEqualityValue old_value = InterfaceEqualityValue::Unknown;
  };

  static std::uint64_t equality_key(core::TermId lhs, core::TermId rhs);
  void rebuild_equality_index();

  const core::TermManager& terms_;
  NelsonOppenInterfaceSummary summary_;
  std::unordered_map<std::uint64_t, std::size_t> equality_index_;
  std::vector<EqualityValueTrailEntry> value_trail_;
  std::vector<std::size_t> level_starts_;
};

// Conservative Track-B scaffold for Nelson-Oppen bookkeeping. It only reports
// user-visible, non-Boolean constants that occur in the current assertions and
// equality atoms already present over those constants. It also reports
// purification sites that would be needed before mixed-theory terms can be sent
// to EUF and LRA as disjoint-theory atoms. It deliberately does not create
// terms/equality atoms, propagate literals, or inspect theory-local state.
NelsonOppenInterfaceSummary analyze_nelson_oppen_interface(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions);

}  // namespace primo::smt
