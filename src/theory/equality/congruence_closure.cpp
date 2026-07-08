#include "theory/equality/congruence_closure.hpp"

#include <stdexcept>

namespace primo::theory::equality {

CongruenceClosure::CongruenceClosure(const core::TermManager& terms) : terms_(terms) {}

void CongruenceClosure::add_term(core::TermId term) {
  terms_.get(term);
  if (term >= seen_.size()) {
    seen_.resize(term + 1, false);
  }
  if (term >= classes_.size()) {
    classes_.reset(terms_.size());
  }
  if (seen_[term]) {
    return;
  }
  seen_[term] = true;
  terms_seen_.push_back(term);
  for (const core::TermId child : terms_.get(term).children) {
    add_term(child);
  }
  dirty_ = true;
}

void CongruenceClosure::assert_equal(core::TermId lhs, core::TermId rhs) {
  add_term(lhs);
  add_term(rhs);
  dirty_ = classes_.unite(lhs, rhs) || dirty_;
}

bool CongruenceClosure::are_equal(core::TermId lhs, core::TermId rhs) {
  add_term(lhs);
  add_term(rhs);
  saturate();
  return classes_.same(lhs, rhs);
}

void CongruenceClosure::saturate() {
  while (dirty_) {
    dirty_ = false;
    for (std::size_t i = 0; i < terms_seen_.size(); ++i) {
      for (std::size_t j = i + 1; j < terms_seen_.size(); ++j) {
        const core::TermId lhs = terms_seen_[i];
        const core::TermId rhs = terms_seen_[j];
        if (!classes_.same(lhs, rhs) && congruent(lhs, rhs)) {
          classes_.unite(lhs, rhs);
          dirty_ = true;
        }
      }
    }
  }
}

bool CongruenceClosure::congruent(core::TermId lhs, core::TermId rhs) const {
  const core::Term& lhs_term = terms_.get(lhs);
  const core::Term& rhs_term = terms_.get(rhs);
  if (lhs_term.kind != rhs_term.kind || lhs_term.symbol != rhs_term.symbol ||
      lhs_term.children.size() != rhs_term.children.size()) {
    return false;
  }
  if (lhs_term.kind != core::TermKind::Application && lhs_term.kind != core::TermKind::Constant) {
    return false;
  }
  for (std::size_t i = 0; i < lhs_term.children.size(); ++i) {
    if (!classes_.same(lhs_term.children[i], rhs_term.children[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace primo::theory::equality
