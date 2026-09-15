#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::theory {

// Equivalence-preserving normalization of quantifier instances, so that
// instances differing only in trivia become one term and can be recognized as
// already generated (docs/quantifiers.md). It is deliberately shallow: every
// rule is local, needs no theory reasoning, and no context from the rest of
// the formula.
//
//   - `(=> a b)` becomes `(or (not a) b)`;
//   - `and`/`or` are flattened, lose neutral constants and duplicates, collapse
//     to the absorbing constant on a constant or a complementary pair `x`,
//     `(not x)`, and keep their operands sorted by TermId;
//   - `(= t t)` is true; a Boolean equality with a constant side is that side
//     or its negation, and `(= x (not x))` is false; other equalities are
//     oriented with the smaller TermId on the left;
//   - `ite` folds a constant condition, removes a negated condition by swapping
//     branches, collapses equal branches, and turns constant Boolean branches
//     into `and`/`or`;
//   - other operators are rebuilt over simplified children through the
//     TermManager constructors, which fold constants.
//
// Quantified subformulas, patterns and leaves are opaque. The result is a fixed
// point: simplifying it again returns it. Results are memoized for the
// lifetime of the simplifier, which is sound because terms are immutable.
class InstanceSimplifier {
 public:
  explicit InstanceSimplifier(core::TermManager& terms);

  core::TermId simplify(core::TermId root);
  std::size_t memoized() const { return memo_.size(); }

 private:
  core::TermId rebuild(core::TermId original, core::TermKind kind,
                       std::vector<core::TermId> children);
  core::TermId connective(core::TermKind kind, std::vector<core::TermId> operands);
  core::TermId equality(core::TermId lhs, core::TermId rhs);
  core::TermId ite(core::TermId condition, core::TermId then_term, core::TermId else_term);

  core::TermManager& terms_;
  std::unordered_map<core::TermId, core::TermId> memo_;
};

}  // namespace primo::theory
