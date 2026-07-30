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

struct NelsonOppenInterfaceSummary {
  std::vector<InterfaceTerm> terms;
  std::vector<InterfaceEquality> visible_equalities;
  std::size_t sort_count = 0;
  std::size_t candidate_pairs = 0;
};

// Conservative Track-B scaffold for Nelson-Oppen bookkeeping. It only reports
// user-visible, non-Boolean constants that occur in the current assertions and
// equality atoms already present over those constants. It deliberately does not
// create equality atoms, propagate literals, or inspect theory-local state.
NelsonOppenInterfaceSummary analyze_nelson_oppen_interface(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions);

}  // namespace primo::smt
