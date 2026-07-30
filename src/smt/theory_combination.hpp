#pragma once

#include <cstddef>
#include <vector>

#include "core/ids.hpp"

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

struct InterfaceEqualityRegistryEntry {
  core::TermId atom = core::kInvalidTermId;
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;
  InterfaceEqualityStatus status = InterfaceEqualityStatus::Missing;
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

// Conservative Track-B scaffold for Nelson-Oppen bookkeeping. It only reports
// user-visible, non-Boolean constants that occur in the current assertions and
// equality atoms already present over those constants. It also reports
// purification sites that would be needed before mixed-theory terms can be sent
// to EUF and LRA as disjoint-theory atoms. It deliberately does not create
// terms/equality atoms, propagate literals, or inspect theory-local state.
NelsonOppenInterfaceSummary analyze_nelson_oppen_interface(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions);

}  // namespace primo::smt
