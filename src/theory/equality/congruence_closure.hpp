#pragma once

#include <vector>

#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "theory/equality/union_find.hpp"

namespace primo::theory::equality {

class CongruenceClosure {
 public:
  explicit CongruenceClosure(const core::TermManager& terms);

  void add_term(core::TermId term);
  void assert_equal(core::TermId lhs, core::TermId rhs);
  void saturate();
  bool are_equal(core::TermId lhs, core::TermId rhs);

 private:
  bool congruent(core::TermId lhs, core::TermId rhs) const;

  const core::TermManager& terms_;
  UnionFind classes_;
  std::vector<core::TermId> terms_seen_;
  std::vector<bool> seen_;
  bool dirty_ = false;
};

}  // namespace primo::theory::equality
