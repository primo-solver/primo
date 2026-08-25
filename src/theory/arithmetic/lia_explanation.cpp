#include "theory/arithmetic/lia_explanation.hpp"

#include <algorithm>

namespace primo::theory::arithmetic {

namespace {

bool lit_less(sat::Lit lhs, sat::Lit rhs) { return lhs.value < rhs.value; }

}  // namespace

void LiaExplanation::add(sat::Lit lit) {
  if (lit.value == kDerivedBoundReason.value) {
    return;
  }
  const auto position = std::lower_bound(literals_.begin(), literals_.end(), lit, lit_less);
  if (position != literals_.end() && position->value == lit.value) {
    return;
  }
  literals_.insert(position, lit);
}

void LiaExplanation::add_all(const std::vector<sat::Lit>& lits) {
  for (const sat::Lit lit : lits) {
    add(lit);
  }
}

void LiaExplanation::add_fixed_bound_reasons(const LraTableau& tableau, LraVarId var) {
  if (const auto lower = tableau.lower_bound(var); lower.has_value()) {
    add(lower->reason);
  }
  if (const auto upper = tableau.upper_bound(var); upper.has_value()) {
    add(upper->reason);
  }
}

}  // namespace primo::theory::arithmetic
