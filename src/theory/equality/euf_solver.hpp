#pragma once

#include <utility>
#include <vector>

#include "core/term.hpp"
#include "core/term_manager.hpp"

namespace primo::theory::equality {

class EufSolver {
 public:
  explicit EufSolver(const core::TermManager& terms);

  void assert_equal(core::TermId lhs, core::TermId rhs);
  void assert_distinct(core::TermId lhs, core::TermId rhs);
  bool is_satisfiable();

 private:
  const core::TermManager& terms_;
  std::vector<std::pair<core::TermId, core::TermId>> equalities_;
  std::vector<std::pair<core::TermId, core::TermId>> disequalities_;
};

}  // namespace primo::theory::equality
