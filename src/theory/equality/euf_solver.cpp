#include "theory/equality/euf_solver.hpp"

#include "theory/equality/congruence_closure.hpp"

namespace primo::theory::equality {

EufSolver::EufSolver(const core::TermManager& terms) : terms_(terms) {}

void EufSolver::assert_equal(core::TermId lhs, core::TermId rhs) {
  terms_.get(lhs);
  terms_.get(rhs);
  equalities_.emplace_back(lhs, rhs);
}

void EufSolver::assert_distinct(core::TermId lhs, core::TermId rhs) {
  terms_.get(lhs);
  terms_.get(rhs);
  disequalities_.emplace_back(lhs, rhs);
}

bool EufSolver::is_satisfiable() {
  CongruenceClosure closure(terms_);
  for (const auto& [lhs, rhs] : equalities_) {
    closure.assert_equal(lhs, rhs);
  }
  for (const auto& [lhs, rhs] : disequalities_) {
    if (closure.are_equal(lhs, rhs)) {
      return false;
    }
  }
  return true;
}

}  // namespace primo::theory::equality
